using System.Text.Json;
using UELib;
using UELib.Core;

namespace Decompiler;

/// <summary>
/// Emits a machine-readable index of the deserialized UnrealScript object model for
/// one or more packages: classes, super classes, fields, functions, states, enums,
/// structs and constants.
///
/// This exists to support *independent verification* of the decompiler output.
/// The index is produced by walking UELib's deserialized object model, which is a
/// different code path from the bytecode decompiler that emits `.uc` text. Comparing
/// the two catches dropped or mangled declarations in the emitted source.
///
/// It doubles as the class/member index needed to drive the .uc -> Rust port.
///
/// Usage: Decompiler index &lt;package.u&gt; &lt;output.json&gt; [&lt;package.u&gt; &lt;output.json&gt; ...]
/// </summary>
public static class IndexExtractor
{
    public static int Run(string[] args)
    {
        if (args.Length == 0 || args.Length % 2 != 0)
        {
            Console.Error.WriteLine(
                "Usage: Decompiler index <package.u> <output.json> [<package.u> <output.json> ...]");
            return 2;
        }

        var pairs = new List<(string path, string outPath)>();
        for (var i = 0; i < args.Length; i += 2)
        {
            var path = args[i];
            if (!File.Exists(path))
            {
                Console.Error.WriteLine($"File not found: {path}");
                return 1;
            }

            pairs.Add((path, args[i + 1]));
        }

        // Load every package before emitting anything, mirroring `gen` mode, so that
        // cross-package super-class references resolve to the same objects.
        var packages = new List<UnrealPackage>(pairs.Count);
        foreach (var (path, _) in pairs)
        {
            Console.WriteLine($"Loading: {path}");
            var pkg = UnrealLoader.LoadPackage(path);
            pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);
            packages.Add(pkg);
        }

        var options = new JsonSerializerOptions
        {
            WriteIndented = false,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        };
        var totalClasses = 0;

        for (var i = 0; i < pairs.Count; i++)
        {
            var (path, outPath) = pairs[i];
            var pkg = packages[i];
            var packageName = Path.GetFileNameWithoutExtension(path);

            var classes = new List<ClassIndex>();
            foreach (var obj in pkg.Objects)
            {
                // Negative indices are imports; only exports belong to this package.
                if (obj is UClass cls && (int)cls > 0)
                {
                    classes.Add(DescribeClass(cls));
                }
            }

            var index = new PackageIndex
            {
                Package = packageName,
                SourceFile = Path.GetFileName(path),
                ClassCount = classes.Count,
                Classes = classes
            };

            var dir = Path.GetDirectoryName(Path.GetFullPath(outPath));
            if (!string.IsNullOrEmpty(dir))
            {
                Directory.CreateDirectory(dir);
            }

            File.WriteAllText(outPath, JsonSerializer.Serialize(index, options));
            totalClasses += classes.Count;
            Console.WriteLine($"  Indexed {classes.Count} classes -> {outPath}");
        }

        Console.WriteLine($"Indexed {totalClasses} classes across {pairs.Count} packages.");
        return 0;
    }

    private static ClassIndex DescribeClass(UClass cls)
    {
        // Loading the record populates Children/fields; `gen` mode does the same before
        // calling Decompile(). Swallow failures the same way so the index stays complete.
        try
        {
            cls.Load<UObjectRecordStream>();
        }
        catch
        {
            // A class that fails to load still gets indexed with whatever is available;
            // the decompiler reports the failure separately.
        }

        var fields = new List<FieldIndex>();
        foreach (var prop in cls.EnumerateFields<UProperty>())
        {
            fields.Add(new FieldIndex
            {
                Name = (string)prop.Name,
                Kind = prop.Type.ToString(),
                ArrayDim = prop.ArrayDim,
                Parm = prop.IsParm()
            });
        }

        var functions = new List<FunctionIndex>();
        foreach (var func in cls.EnumerateFields<UFunction>())
        {
            var parms = new List<string>();
            UProperty? ret = null;
            try
            {
                // Mirror UFunction.Params (obsolete) without the deprecation warning,
                // and skip the return parm, which is reported separately.
                foreach (var p in func.EnumerateFields<UProperty>())
                {
                    if (!p.IsParm() || p.PropertyFlags.HasFlag(UELib.Flags.PropertyFlag.ReturnParm))
                    {
                        continue;
                    }

                    parms.Add((string)p.Name);
                }

                ret = func.ReturnProperty;
            }
            catch
            {
                // Parameter list unavailable; record the function by name anyway.
            }

            functions.Add(new FunctionIndex
            {
                Name = (string)func.Name,
                // The emitter prints FriendlyName (the source-level name, e.g. the
                // operator symbol `!=`), not Name (which for cooked operator
                // functions is the exec name such as `NotEqual_ObjectObject`).
                FriendlyName = func.FriendlyName is null ? null : (string)func.FriendlyName,
                Native = func.NativeToken > 0,
                Params = parms,
                Return = ret is null ? null : (string)ret.Name
            });
        }

        var states = new List<string>();
        foreach (var state in cls.EnumerateFields<UState>())
        {
            states.Add((string)state.Name);
        }

        var enums = new List<EnumIndex>();
        foreach (var en in cls.EnumerateFields<UEnum>())
        {
            var values = new List<string>();
            try
            {
                if (en.Names != null)
                {
                    foreach (var name in en.Names)
                    {
                        values.Add((string)name);
                    }
                }
            }
            catch
            {
                // Enum values unavailable; the name is still checked.
            }

            enums.Add(new EnumIndex { Name = (string)en.Name, Values = values });
        }

        var structs = new List<string>();
        foreach (var st in cls.EnumerateFields<UStruct>())
        {
            if (st.IsPureStruct())
            {
                structs.Add((string)st.Name);
            }
        }

        var consts = new List<string>();
        foreach (var c in cls.EnumerateFields<UConst>())
        {
            consts.Add((string)c.Name);
        }

        return new ClassIndex
        {
            Name = (string)cls.Name,
            Super = cls.Super is null ? null : (string)cls.Super.Name,
            Fields = fields,
            Functions = functions,
            States = states,
            Enums = enums,
            Structs = structs,
            Consts = consts
        };
    }

    public sealed class PackageIndex
    {
        public string Package { get; set; } = string.Empty;
        public string SourceFile { get; set; } = string.Empty;
        public int ClassCount { get; set; }
        public List<ClassIndex> Classes { get; set; } = new();
    }

    public sealed class ClassIndex
    {
        public string Name { get; set; } = string.Empty;
        public string? Super { get; set; }
        public List<FieldIndex> Fields { get; set; } = new();
        public List<FunctionIndex> Functions { get; set; } = new();
        public List<string> States { get; set; } = new();
        public List<EnumIndex> Enums { get; set; } = new();
        public List<string> Structs { get; set; } = new();
        public List<string> Consts { get; set; } = new();
    }

    public sealed class FieldIndex
    {
        public string Name { get; set; } = string.Empty;
        public string Kind { get; set; } = string.Empty;
        public int ArrayDim { get; set; }
        public bool Parm { get; set; }
    }

    public sealed class FunctionIndex
    {
        public string Name { get; set; } = string.Empty;
        public string? FriendlyName { get; set; }
        public bool Native { get; set; }
        public List<string> Params { get; set; } = new();
        public string? Return { get; set; }
    }

    public sealed class EnumIndex
    {
        public string Name { get; set; } = string.Empty;
        public List<string> Values { get; set; } = new();
    }
}
