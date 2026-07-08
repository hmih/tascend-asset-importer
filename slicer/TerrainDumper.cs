using System;
using System.IO;
using System.Linq;
using UELib;
using UELib.Core;

namespace Decompiler;

/// <summary>
/// Dumps raw binary data of Terrain and TerrainComponent exports from UE3 packages.
/// </summary>
public static class TerrainDumper
{
    public static int Run(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("Usage: Decompiler terrain-dump <map.fmap> <output-dir>");
            return 1;
        }

        var mapPath = args[0];
        var outputDir = args[1];
        Directory.CreateDirectory(outputDir);

        if (UnrealConfig.VariableTypes == null)
            UnrealConfig.VariableTypes = new();

        Console.WriteLine($"Loading: {mapPath}");
        var pkg = UnrealLoader.LoadPackage(mapPath);
        pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);
        Console.WriteLine($"  Version: {pkg.Summary.Version}, Objects: {pkg.Objects.Count}");

        int count = 0;
        foreach (var obj in pkg.Objects)
        {
            if ((int)obj <= 0) continue;
            string className = obj.Class?.Name?.ToString() ?? "Unknown";
            if (className != "Terrain" && className != "TerrainComponent") continue;
            if (count >= 5) break; // Just a few for analysis

            Console.WriteLine($"  Found {className}: {obj.Name} (outer={obj.Outer?.Name})");

            try
            {
                obj.Load<UObjectRecordStream>();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"    Failed to load: {ex.Message}");
                continue;
            }

            var buffer = obj.CopyBuffer();
            if (buffer == null)
            {
                Console.Error.WriteLine("    No buffer available");
                continue;
            }

            Console.WriteLine($"    Buffer size: {buffer.Length} bytes");

            var safeName = $"{className}_{obj.Name}_{count}".Replace('/', '_');
            var outPath = Path.Combine(outputDir, $"{safeName}.bin");
            File.WriteAllBytes(outPath, buffer);

            // Hex dump first 1024 bytes
            var hexPath = Path.Combine(outputDir, $"{safeName}.hex.txt");
            using (var sw = new StreamWriter(hexPath))
            {
                int dumpLen = Math.Min(buffer.Length, 2048);
                for (int i = 0; i < dumpLen; i += 16)
                {
                    var hex = string.Join(" ", Enumerable.Range(i, Math.Min(16, dumpLen - i))
                        .Select(j => buffer[j].ToString("X2")));
                    var ascii = string.Join("", Enumerable.Range(i, Math.Min(16, dumpLen - i))
                        .Select(j => buffer[j] >= 32 && buffer[j] < 127 ? (char)buffer[j] : '.'));
                    sw.WriteLine($"{i:X6}: {hex,-48} {ascii}");
                }
            }

            // Property dump
            var propPath = Path.Combine(outputDir, $"{safeName}.props.txt");
            using (var sw = new StreamWriter(propPath))
            {
                sw.WriteLine($"Object: {obj.Name}");
                sw.WriteLine($"Class: {className}");
                sw.WriteLine($"Outer: {obj.Outer?.Name}");
                sw.WriteLine($"ExportTable SerialOffset: {obj.ExportTable?.SerialOffset}");
                sw.WriteLine($"ExportTable SerialSize: {obj.ExportTable?.SerialSize}");
                sw.WriteLine($"Buffer length: {buffer.Length}");
                sw.WriteLine();
                if (obj.Properties != null)
                {
                    foreach (var prop in obj.Properties)
                    {
                        sw.WriteLine($"Property: {prop.Name} (type={prop.Type}, size={prop.Size})");
                        try { sw.WriteLine($"  Value: {prop.Decompile()}"); }
                        catch (Exception ex) { sw.WriteLine($"  Value: /* ERROR: {ex.Message} */"); }
                    }
                }
            }

            Console.WriteLine($"    Wrote: {outPath}");
            count++;
        }

        Console.WriteLine($"Dumped {count} terrain objects to {outputDir}");
        return 0;
    }
}
