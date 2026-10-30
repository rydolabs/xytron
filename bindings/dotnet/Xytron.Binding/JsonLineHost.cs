using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace Xytron;

/// <summary>
/// Experimental, single-owner JSONL worker over PRIVATE parent/child pipes.
/// This is not a public RPC listener or NW-014 policy implementation. A trusted
/// native broker must authorize origin/frame BEFORE forwarding an operation,
/// provision a per-session owner, and terminate the child when that session ends.
/// </summary>
public static class JsonLineHost
{
    public const int MaxFrameBytes = 65536;
    public const int MaxRequestId = int.MaxValue;

    private sealed class RequestRejected : Exception { }

    // Bounded to prevent arbitrary line buffers from being allocated.
    // Oversized frames terminate processing: framing cannot be recovered safely.
    private static string? ReadBoundedLine(TextReader input, out string? framingError)
    {
        framingError = null;
        var text = new StringBuilder();
        while (true)
        {
            int ch = input.Read();
            if (ch == -1)
            {
                if (text.Length != 0) framingError = "InvalidFrame";
                return null;
            }
            if (ch == '\n') return text.ToString().TrimEnd('\r');
            if (text.Length >= MaxFrameBytes) { framingError = "FrameTooLarge"; return null; }
            text.Append((char)ch);
        }
    }

    private static (int Id, string Operation, object?[] Arguments) ReadRequest(string line, int previousId)
    {
        if (Encoding.UTF8.GetByteCount(line) > MaxFrameBytes) throw new RequestRejected();
        using var doc = JsonDocument.Parse(line, new JsonDocumentOptions { MaxDepth=8 });
        var root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object) throw new RequestRejected();
        var fields = new HashSet<string>(StringComparer.Ordinal);
        foreach (var field in root.EnumerateObject())
            if (!fields.Add(field.Name)) throw new RequestRejected();
        if (!fields.SetEquals(new[] { "id", "operation", "args" })) throw new RequestRejected();
        var idNode = root.GetProperty("id");
        if (idNode.ValueKind != JsonValueKind.Number || !idNode.TryGetInt32(out int id) ||
            id <= previousId) throw new RequestRejected();
        var opNode = root.GetProperty("operation");
        if (opNode.ValueKind != JsonValueKind.String) throw new RequestRejected();
        string operation = opNode.GetString() ?? "";
        if (operation.Length is < 1 or > 256) throw new RequestRejected();
        var arguments = root.GetProperty("args");
        if (arguments.ValueKind != JsonValueKind.Array || arguments.GetArrayLength() > 32)
            throw new RequestRejected();
        var result = new List<object?>();
        foreach (var arg in arguments.EnumerateArray())
        {
            if (arg.ValueKind is not (JsonValueKind.Null or JsonValueKind.String or
                JsonValueKind.Number or JsonValueKind.True or JsonValueKind.False))
                throw new RequestRejected();
            if (arg.ValueKind == JsonValueKind.Number &&
                (!arg.TryGetDouble(out double numeric) || !double.IsFinite(numeric)))
                throw new RequestRejected();
            result.Add(arg.Clone());
        }
        return (id, operation, result.ToArray());
    }

    private static void Reply(TextWriter output, int? id, bool ok, object? result=null, string? error=null)
    {
        string json;
        try
        {
            json = ok ? JsonSerializer.Serialize(new { id, ok, result })
                      : JsonSerializer.Serialize(new { id, ok, error });
            if (Encoding.UTF8.GetByteCount(json) > MaxFrameBytes)
                throw new JsonException("Result exceeds frame size");
        }
        catch (Exception ex) when (ex is JsonException or NotSupportedException or ArgumentException)
        {
            json = JsonSerializer.Serialize(new { id, ok=false, error="InvalidResult" });
        }
        output.WriteLine(json);
        output.Flush();
    }

    /// <summary>Owner comes from trusted parent bootstrap, never from request JSON.</summary>
    public static void Run(TextReader input, TextWriter output, ObjectHost host, string owner)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(host);
        if (string.IsNullOrWhiteSpace(owner)) throw new ArgumentException("Missing trusted owner", nameof(owner));
        int previousId = 0;
        // Preserve the original IPC writer, but redirect ordinary plugin logs
        // away from stdout. Run this only inside the dedicated child process.
        var originalOut = Console.Out;
        bool redirect = ReferenceEquals(output, originalOut);
        if (redirect) Console.SetOut(Console.Error);
        try
        {
            while (true)
            {
                var line = ReadBoundedLine(input, out string? framingError);
                if (framingError is not null)
                {
                    Reply(output, null, false, error:framingError);
                    break;
                }
                if (line is null) break;
                int id;
                string operation;
                object?[] args;
                try
                {
                    (id, operation, args) = ReadRequest(line, previousId);
                }
                catch (Exception ex) when (ex is RequestRejected or JsonException or OverflowException)
                {
                    Reply(output, null, false, error:"InvalidRequest");
                    continue;
                }
                previousId = id;
                // The first argument of any non-create class operation is an
                // opaque object handle. ObjectHost requires an actual string;
                // retain JsonElement for remaining typed method arguments.
                if (!operation.EndsWith(".create", StringComparison.Ordinal) &&
                    args.Length > 0 && args[0] is JsonElement handle &&
                    handle.ValueKind == JsonValueKind.String)
                    args[0] = handle.GetString();
                try
                {
                    object? value = host.Invoke(owner, operation, args);
                    Reply(output, id, true, result:value);
                }
                catch (ObjectHostException)
                {
                    Reply(output, id, false, error:"Rejected");
                }
                catch (Exception)
                {
                    // Managed exceptions and their messages are never serialized.
                    Reply(output, id, false, error:"PluginError");
                }
            }
        }
        finally
        {
            try { host.Cleanup(owner); }
            finally { if (redirect) Console.SetOut(originalOut); }
        }
    }
}
