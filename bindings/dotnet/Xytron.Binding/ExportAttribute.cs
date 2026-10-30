using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace Xytron;

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method | AttributeTargets.Constructor | AttributeTargets.Property,
    AllowMultiple = false, Inherited = false)]
public sealed class ExportAttribute : Attribute { }

public sealed class ExportMetadata
{
    public string Name { get; }
    public string Kind { get; }
    public IReadOnlyList<string> Methods { get; }
    public IReadOnlyList<string> Properties { get; }

    internal ExportMetadata(string name, string kind,
        IEnumerable<string>? methods = null,
        IEnumerable<string>? properties = null)
    {
        Name = name;
        Kind = kind;
        Methods = (methods ?? Array.Empty<string>()).OrderBy(x => x).ToArray();
        Properties = (properties ?? Array.Empty<string>()).OrderBy(x => x).ToArray();
    }
}

/// <summary>Local metadata discovery only; no language host or JS IPC.</summary>
public static class ExportDiscovery
{
    public static IReadOnlyList<ExportMetadata> Discover(Assembly assembly)
    {
        var exports = new List<ExportMetadata>();
        var known = new HashSet<string>(StringComparer.Ordinal);
        foreach (var type in assembly.GetExportedTypes())
        {
            if (type.IsDefined(typeof(ExportAttribute), false))
            {
                Add(new ExportMetadata(type.Name, "class",
                    type.GetMethods(BindingFlags.DeclaredOnly | BindingFlags.Public |
                                    BindingFlags.Instance)
                        .Where(m => !m.IsSpecialName && m.IsDefined(typeof(ExportAttribute), false))
                        .Select(m => LowerFirst(m.Name)),
                    type.GetProperties(BindingFlags.DeclaredOnly |
                                       BindingFlags.Public | BindingFlags.Instance)
                        .Where(p => p.IsDefined(typeof(ExportAttribute), false))
                        .Select(p => LowerFirst(p.Name))),
                    exports, known);
            }
            foreach (var method in type.GetMethods(
                BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.Static))
            {
                if (method.IsDefined(typeof(ExportAttribute), false))
                    Add(new ExportMetadata(LowerFirst(method.Name), "function"),
                        exports, known);
            }
        }
        return exports;
    }

    private static string LowerFirst(string name) =>
        name.Length == 0 ? name : char.ToLowerInvariant(name[0]) + name[1..];

    private static void Add(ExportMetadata item, List<ExportMetadata> exports,
                            HashSet<string> known)
    {
        if (!known.Add(item.Name))
            throw new InvalidOperationException("Duplicate Xytron export: " + item.Name);
        exports.Add(item);
    }
}
