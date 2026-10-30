using System;
using System.Text.Json;
using Xytron;

[Export]
public sealed class Player : IDisposable
{
    public static int Disposed;
    [Export] public Player(string name) { Name = name; }
    public string Name { get; }
    [Export] public int Attack(int damage) { Health -= damage; return Health; }
    [Export] public int Health { get; set; } = 100;
    [Export] public float Scale(float input) => input * 2;
    public int Field = 17;
    public string Secret() => "private";
    public void Dispose() => Disposed++;
}

[Export]
public sealed class ReadOnly
{
    [Export] public int Value { get; } = 17;
}

public sealed class Unmarked
{
    public int Value { get; set; }
}

internal static class Program
{
    private static int cases;
    private static void Check(bool condition, string description)
    {
        if (!condition) throw new Exception(description);
        cases++;
    }
    private static void Denied(Action action)
    {
        try { action(); }
        catch (ObjectHostException) { cases++; return; }
        throw new Exception("Expected denial");
    }
    private static void Invalid(Action action)
    {
        try { action(); }
        catch (ArgumentException) { cases++; return; }
        throw new Exception("Expected invalid template");
    }
    public static void Main()
    {
        Player.Disposed = 0;
        const string op = "example_cs.Player.";
        var host = new ObjectHost("example_cs", new[] { typeof(Player), typeof(ReadOnly) },
            (owner, operation) => owner != "blocked" && operation.StartsWith("example_cs."),
            maximumObjects: 2);
        var handle = (string)host.Invoke("frame-A", op + "create", "Alice")!;
        Check(handle.StartsWith("xy_") && handle.Length > 30, "opaque handle");
        Check(host.LiveCount == 1, "tracked object");
        Check((int)host.Invoke("frame-A", op + "method.Attack", handle, 9)! == 91, "exported call");
        Check((int)host.Invoke("frame-A", op + "get.Health", handle)! == 91, "property read");
        host.Invoke("frame-A", op + "set.Health", handle, 55);
        Check((int)host.Invoke("frame-A", op + "get.Health", handle)! == 55, "property write");
        using (var json = JsonDocument.Parse("23"))
            host.Invoke("frame-A", op + "set.Health", handle, json.RootElement);
        Check((int)host.Invoke("frame-A", op + "get.Health", handle)! == 23, "JSON numeric argument");
        using (var real = JsonDocument.Parse("1.5"))
            Check((float)host.Invoke("frame-A", op + "method.Scale", handle, real.RootElement)! == 3f,
                  "JSON float conversion and reflection dispatch");
        Denied(() => host.Invoke("frame-B", op + "get.Health", handle));
        Denied(() => host.Invoke("blocked", op + "get.Health", handle));
        Denied(() => host.Invoke("frame-A", op + "method.Secret", handle));
        Denied(() => host.Invoke("frame-A", op + "get.Field", handle));
        Denied(() => host.Invoke("frame-A", op + "method.__class__", handle));
        Denied(() => host.Invoke("frame-A", "other.Player.get.Health", handle));
        Denied(() => host.Invoke("frame-A", op + "method.Attack", handle, true));
        Denied(() => host.Invoke("frame-A", op + "method.Attack", handle, 1, 2));
        Denied(() => host.Invoke("frame-A", op + "set.Health", handle, "invalid"));
        var second = (string)host.Invoke("frame-B", op + "create", "Bob")!;
        Denied(() => host.Invoke("frame-A", op + "create", "Limit"));
        Check(host.Cleanup("frame-A") == 1 && Player.Disposed == 1, "disconnect cleanup");
        Denied(() => host.Invoke("frame-A", op + "get.Health", handle));
        Check((int)host.Invoke("frame-B", op + "get.Health", second)! == 100, "foreign retained");
        Check(host.Cleanup("frame-B") == 1 && Player.Disposed == 2, "other owner cleanup");
        var third = (string)host.Invoke("frame-A", op + "create", "Carol")!;
        host.Invoke("frame-A", op + "dispose", third);
        Check(Player.Disposed == 3 && host.LiveCount == 0, "IDisposable called");
        Denied(() => host.Invoke("frame-A", op + "dispose", third));
        var readonlyHandle = (string)host.Invoke("frame-A", "example_cs.ReadOnly.create")!;
        Check((int)host.Invoke("frame-A", "example_cs.ReadOnly.get.Value", readonlyHandle)! == 17, "read-only property");
        Denied(() => host.Invoke("frame-A", "example_cs.ReadOnly.set.Value", readonlyHandle, 4));
        Check(host.Cleanup("frame-A") == 1, "read-only cleanup");
        Denied(() => host.Invoke("frame-A", op + "get.Health", readonlyHandle));
        Invalid(() => new ObjectHost("example_cs", new[] { typeof(Unmarked) }, (_, _) => true));
        Invalid(() => new ObjectHost("example_cs", new[] { typeof(Player) }, (_, _) => true, 0));
        var deny = new ObjectHost("example_cs", new[] { typeof(Player) }, (_, _) => false);
        Denied(() => deny.Invoke("frame-A", op + "create", "Alice"));
        var failedPolicy = new ObjectHost("example_cs", new[] { typeof(Player) },
            (_, _) => throw new InvalidOperationException("Unavailable"));
        Denied(() => failedPolicy.Invoke("frame-A", op + "create", "Alice"));
        Console.WriteLine($"PASS: {cases} .NET ObjectHost checks");
    }
}
