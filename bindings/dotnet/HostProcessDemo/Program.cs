using Xytron;

// Test/demo child only. Real native brokers must provision session identity and
// implement NW-014 permissions before forwarding requests over private pipes.
[Export]
public sealed class Player : IDisposable
{
    public string Name { get; }
    [Export] public Player(string name) => Name=name;
    [Export] public int Hit(int amount) { Console.WriteLine("PLUGIN_LOG"); Health -= amount; return Health; }
    [Export] public int Health { get; set; } = 100;
    public string Secret() => "not exported";
    public void Dispose() => Console.Error.WriteLine("CLOSED_DOTNET");
}

internal static class Program
{
    private static int Main()
    {
        var host = new ObjectHost("example_cs", new[] {typeof(Player)},
            (owner, op) => owner=="session_1" && op.StartsWith("example_cs.", StringComparison.Ordinal));
        JsonLineHost.Run(Console.In, Console.Out, host, "session_1");
        return 0;
    }
}
