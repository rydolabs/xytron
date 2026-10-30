using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Xytron;

public sealed class ObjectHostException : InvalidOperationException
{
    public ObjectHostException(string message) : base(message) { }
}

/// <summary>
/// Experimental in-process class dispatcher. Owner identity MUST come from the
/// trusted native bridge, never a JavaScript argument. The authorization callback
/// must implement NW-014 separately. This is not IPC or a sandbox.
/// </summary>
public sealed class ObjectHost
{
    private sealed record Entry(string Owner, Type Class, object Instance);
    private readonly string _pluginId;
    private readonly Func<string, string, bool> _authorize;
    private readonly int _max;
    private readonly object _gate = new();
    private readonly Dictionary<string, Type> _classes = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Entry> _objects = new(StringComparer.Ordinal);

    public ObjectHost(string pluginId, IEnumerable<Type> classes,
                      Func<string, string, bool> authorize, int maximumObjects = 1024)
    {
        if (string.IsNullOrEmpty(pluginId) ||
            !Regex.IsMatch(pluginId, @"\A[A-Za-z][A-Za-z0-9_]*\z"))
            throw new ArgumentException("Invalid plugin ID");
        _authorize = authorize ?? throw new ArgumentNullException(nameof(authorize));
        if (classes is null) throw new ArgumentNullException(nameof(classes));
        if (maximumObjects <= 0) throw new ArgumentOutOfRangeException(nameof(maximumObjects));
        _pluginId = pluginId;
        _max = maximumObjects;
        foreach (var t in classes)
        {
            if (t is null || !t.IsClass || t.IsAbstract || t.ContainsGenericParameters ||
                !(t.IsPublic || t.IsNestedPublic) ||
                !t.IsDefined(typeof(ExportAttribute), false))
                throw new ArgumentException("Only explicitly exported public concrete classes are supported");
            var ctors = t.GetConstructors(BindingFlags.Public | BindingFlags.Instance);
            var marked = ctors.Where(c => c.IsDefined(typeof(ExportAttribute), false)).ToArray();
            if (marked.Length > 1 || (marked.Length == 0 && !ctors.Any(c => c.GetParameters().Length == 0)))
                throw new ArgumentException("Exported constructor is ambiguous or missing");
            var ctor = marked.Length == 1 ? marked[0] : ctors.Single(c => c.GetParameters().Length == 0);
            Validate(ctor.GetParameters());
            var methods = t.GetMethods(BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.Instance)
                           .Where(m => !m.IsSpecialName && m.IsDefined(typeof(ExportAttribute), false)).ToArray();
            if (methods.GroupBy(m => m.Name, StringComparer.Ordinal).Any(g => g.Count() > 1))
                throw new ArgumentException("Overloaded exports not supported");
            foreach (var m in methods)
            {
                if (m.IsGenericMethod || m.ContainsGenericParameters) throw new ArgumentException("Generic export");
                Validate(m.GetParameters());
                TypeSupported(m.ReturnType, true);
            }
            var props = t.GetProperties(BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.Instance)
                         .Where(p => p.IsDefined(typeof(ExportAttribute), false)).ToArray();
            foreach (var p in props)
            {
                if (p.GetIndexParameters().Length != 0 ||
                    !(p.GetMethod?.IsPublic == true || p.SetMethod?.IsPublic == true))
                    throw new ArgumentException("Unsupported exported property");
                TypeSupported(p.PropertyType, false);
            }
            if (methods.Length == 0 && props.Length == 0)
                throw new ArgumentException("Exported class requires explicit members");
            if (!_classes.TryAdd(t.Name, t)) throw new ArgumentException("Duplicate class");
        }
        if (_classes.Count == 0) throw new ArgumentException("No classes");
    }

    public int LiveCount { get { lock (_gate) return _objects.Count; } }

    public object? Invoke(string owner, string operation, params object?[] args)
    {
        if (string.IsNullOrWhiteSpace(owner) || string.IsNullOrEmpty(operation))
            throw new ObjectHostException("Missing owner or operation");
        bool allowed;
        try { allowed = _authorize(owner, operation); }
        catch { throw new ObjectHostException("Authorization unavailable"); }
        if (!allowed) throw new ObjectHostException("Permission denied");
        var path = operation.Split('.');
        if ((path.Length != 3 && path.Length != 4) || path[0] != _pluginId ||
            !_classes.TryGetValue(path[1], out var type))
            throw new ObjectHostException("Unknown class operation");
        args ??= Array.Empty<object?>();
        lock (_gate)
        {
            if (path.Length == 3 && path[2] == "create")
            {
                if (_objects.Count >= _max) throw new ObjectHostException("Object limit");
                var ctors = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance);
                var marked = ctors.Where(c => c.IsDefined(typeof(ExportAttribute), false)).ToArray();
                var ctor = marked.Length == 1 ? marked[0] : ctors.Single(c => c.GetParameters().Length == 0);
                var converted = Arguments(ctor.GetParameters(), args);
                object instance;
                try { instance = ctor.Invoke(converted); }
                catch (TargetInvocationException) { throw new ObjectHostException("Constructor failed"); }
                string handle;
                do { handle = "xy_" + Convert.ToHexString(RandomNumberGenerator.GetBytes(24)); }
                while (_objects.ContainsKey(handle));
                _objects.Add(handle, new Entry(owner, type, instance));
                return handle;
            }
            if (args.Length == 0 || args[0] is not string key ||
                !_objects.TryGetValue(key, out var entry) ||
                entry.Owner != owner || entry.Class != type)
                throw new ObjectHostException("Foreign or unknown handle");
            var rest = args.Skip(1).ToArray();
            if (path.Length == 3 && path[2] == "dispose")
            {
                if (rest.Length != 0) throw new ObjectHostException("Invalid dispose arguments");
                _objects.Remove(key);
                (entry.Instance as IDisposable)?.Dispose();
                return null;
            }
            if (path.Length != 4 || path[3].StartsWith('_'))
                throw new ObjectHostException("Unknown member");
            if (path[2] == "method")
            {
                var matches = type.GetMethods(BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.Instance)
                                  .Where(m => !m.IsSpecialName && m.Name == path[3] &&
                                        m.IsDefined(typeof(ExportAttribute), false)).ToArray();
                if (matches.Length != 1) throw new ObjectHostException("Unexported method");
                try { return matches[0].Invoke(entry.Instance, Arguments(matches[0].GetParameters(), rest)); }
                catch (TargetInvocationException) { throw new ObjectHostException("Method failed"); }
            }
            if (path[2] == "get" || path[2] == "set")
            {
                var property = type.GetProperty(path[3], BindingFlags.DeclaredOnly |
                                                            BindingFlags.Public | BindingFlags.Instance);
                if (property is null || !property.IsDefined(typeof(ExportAttribute), false))
                    throw new ObjectHostException("Unexported property");
                if (path[2] == "get")
                {
                    if (rest.Length != 0 || property.GetMethod?.IsPublic != true)
                        throw new ObjectHostException("Property not readable");
                    try { return property.GetValue(entry.Instance); }
                    catch (TargetInvocationException) { throw new ObjectHostException("Getter failed"); }
                }
                if (property.SetMethod?.IsPublic != true)
                    throw new ObjectHostException("Property not writable");
                var converted = Arguments(property.SetMethod.GetParameters(), rest);
                try { property.SetValue(entry.Instance, converted[0]); }
                catch (TargetInvocationException) { throw new ObjectHostException("Setter failed"); }
                return null;
            }
            throw new ObjectHostException("Unsupported operation");
        }
    }

    /// <summary>Only trusted native host should call on session disconnect.</summary>
    public int Cleanup(string owner)
    {
        if (string.IsNullOrWhiteSpace(owner)) throw new ObjectHostException("Missing owner");
        lock (_gate)
        {
            var items = _objects.Where(p => p.Value.Owner == owner).ToArray();
            foreach (var item in items) _objects.Remove(item.Key);
            foreach (var item in items)
                try { (item.Value.Instance as IDisposable)?.Dispose(); }
                catch { /* invalidate remaining objects even when Dispose throws */ }
            return items.Length;
        }
    }

    private static void TypeSupported(Type t, bool voidAllowed)
    {
        if ((voidAllowed && t == typeof(void)) || t == typeof(string) ||
            t == typeof(bool) || t == typeof(byte[]) || t == typeof(int) ||
            t == typeof(long) || t == typeof(short) || t == typeof(float) ||
            t == typeof(double)) return;
        throw new ArgumentException("Unsupported export type");
    }

    private static void Validate(ParameterInfo[] parameters)
    {
        foreach (var p in parameters)
        {
            if (p.IsOptional || p.HasDefaultValue || p.IsOut || p.ParameterType.IsByRef ||
                p.IsDefined(typeof(ParamArrayAttribute), false))
                throw new ArgumentException("Unsupported parameter form");
            TypeSupported(p.ParameterType, false);
        }
    }

    private static object?[] Arguments(ParameterInfo[] parameters, object?[] values)
    {
        if (parameters.Length != values.Length) throw new ObjectHostException("Invalid argument count");
        var result = new object?[values.Length];
        for (int i = 0; i < values.Length; i++) result[i] = ConvertValue(values[i], parameters[i].ParameterType);
        return result;
    }

    private static object ConvertValue(object? value, Type target)
    {
        if (value is JsonElement json)
        {
            try
            {
                if (target == typeof(string) && json.ValueKind == JsonValueKind.String)
                    return json.GetString()!;
                if (target == typeof(bool) && (json.ValueKind == JsonValueKind.True ||
                                                json.ValueKind == JsonValueKind.False))
                    return json.GetBoolean();
                if (target == typeof(byte[]) && json.ValueKind == JsonValueKind.String)
                    return json.GetBytesFromBase64();
                if (json.ValueKind == JsonValueKind.Number)
                {
                    if (target == typeof(int) && json.TryGetInt32(out var n)) return n;
                    if (target == typeof(short) && json.TryGetInt32(out var s) &&
                        s >= short.MinValue && s <= short.MaxValue) return (short)s;
                    if (target == typeof(long) && json.TryGetInt64(out var l) &&
                        l >= -9007199254740991L && l <= 9007199254740991L) return l;
                    if (target == typeof(float) || target == typeof(double))
                    {
                        var d = json.GetDouble();
                        if (double.IsFinite(d) && (target == typeof(double) || Math.Abs(d) <= float.MaxValue))
                            return target == typeof(double) ? (object)d : (float)d;
                    }
                }
            }
            catch (Exception e) when (e is FormatException or InvalidOperationException or OverflowException) { }
            throw new ObjectHostException("Invalid JSON argument");
        }
        if (value is null || value.GetType() != target) throw new ObjectHostException("Invalid argument type");
        if (target == typeof(long) && Math.Abs((double)(long)value) > 9007199254740991d)
            throw new ObjectHostException("Unsafe JavaScript integer");
        if (target == typeof(float) && !float.IsFinite((float)value))
            throw new ObjectHostException("Nonfinite float");
        if (target == typeof(double) && !double.IsFinite((double)value))
            throw new ObjectHostException("Nonfinite double");
        return value;
    }
}
