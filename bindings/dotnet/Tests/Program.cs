using System;
using System.Linq;
using Xytron;

public static class Functions
{
    [Export] public static int Add(int a, int b) => a + b;
    public static int NotExported(int x) => x;
}

[Export]
public class Player
{
    [Export] public int Hp { get; set; } = 100;
    [Export] public void Move(float x, float y) { }
    private void Secret() { }
    public void Unmarked() { }
    public string Field = "";
}

internal static class Program
{
    static void Main()
    {
        var items = ExportDiscovery.Discover(typeof(Functions).Assembly);
        if (items.Count != 2)
            throw new Exception($"Expected 2 exported items, got {items.Count}");
        var add = items.Single(x => x.Name == "add");
        if (add.Kind != "function") throw new Exception("Wrong function metadata");
        var player = items.Single(x => x.Name == "Player");
        if (player.Kind != "class" ||
            !player.Methods.SequenceEqual(new [] {"move"}) ||
            !player.Properties.SequenceEqual(new [] {"hp"}))
            throw new Exception("Class method/property export mismatch");
        if (items.Any(x => x.Name == "notExported" || x.Name == "Secret"))
            throw new Exception("Unmarked/private API exposed");
        if (Functions.Add(20, 22) != 42)
            throw new Exception("Test fixture failed");
        Console.WriteLine("Xytron C# [Export] local metadata: PASS");
    }
}
