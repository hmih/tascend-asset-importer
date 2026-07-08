using System.Text.Json;
using System.Text.Json.Serialization;
using UELib;
using UELib.Core;
using UELib.Types;

namespace Decompiler;

/// <summary>
/// Extracts map data from UE3 .fmap files (renamed UE3 packages).
/// Dumps actor placements (positions, rotations, class refs), WorldInfo,
/// static mesh instances, lights, navigation, and Kismet sequence events.
/// </summary>
public static class MapExtractor
{
    public static int Run(string[] args)
    {
        if (args.Length == 0 || (args.Length == 1 && args[0] == "--help"))
        {
            Console.Error.WriteLine("Usage: Decompiler maps <map.fmap> <output.json> [<map.fmap> <output.json> ...]");
            Console.Error.WriteLine("       Decompiler maps --dir <maps-dir> <output-dir>");
            return 1;
        }

        if (UnrealConfig.VariableTypes == null)
            UnrealConfig.VariableTypes = new();
        RegisterArrayTypeHints();

        // --dir mode: process all .fmap files in a directory
        if (args[0] == "--dir")
        {
            if (args.Length < 3)
            {
                Console.Error.WriteLine("Usage: Decompiler maps --dir <maps-dir> <output-dir>");
                return 1;
            }

            var mapsDir = args[1];
            var outputDir = args[2];
            Directory.CreateDirectory(outputDir);

            var maps = Directory.GetFiles(mapsDir, "*.fmap", SearchOption.AllDirectories);
            Console.WriteLine($"Found {maps.Length} .fmap files in {mapsDir}");

            // Group by map name — keep only the main map file (not _Sound/_Ter/_Cameras sub-maps)
            // by processing them all but naming output after the main map.
            // Actually, let's process all of them — they're separate packages.
            var pairs = new List<(string path, string output)>();
            foreach (var fmap in maps)
            {
                var relPath = Path.GetRelativePath(mapsDir, fmap);
                var mapName = Path.GetFileNameWithoutExtension(fmap);
                var outputPath = Path.Combine(outputDir, relPath.Replace(".fmap", ".json"));
                pairs.Add((fmap, outputPath));
            }

            return RunPairs(pairs.ToArray());
        }

        // Pairs mode
        if (args.Length % 2 != 0)
        {
            Console.Error.WriteLine("Expected pairs of <map.fmap> <output.json>");
            return 1;
        }

        var pairsList = new List<(string path, string output)>();
        for (int i = 0; i < args.Length; i += 2)
        {
            pairsList.Add((args[i], args[i + 1]));
        }
        return RunPairs(pairsList.ToArray());
    }

    private static int RunPairs((string path, string output)[] pairs)
    {
        int totalMaps = 0;
        int totalErrors = 0;

        foreach (var (mapPath, outputPath) in pairs)
        {
            if (!File.Exists(mapPath))
            {
                Console.Error.WriteLine($"File not found: {mapPath}");
                return 1;
            }

            try
            {
                Console.WriteLine($"Loading: {mapPath}");
                var pkg = UnrealLoader.LoadPackage(mapPath);
                pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);
                Console.WriteLine($"  Version: {pkg.Summary.Version}, Objects: {pkg.Objects.Count}");

                var mapName = Path.GetFileNameWithoutExtension(mapPath);
                var objects = new List<JsonElement>();
                int mapObjects = 0;
                int mapErrors = 0;

                // Class statistics
                var classCounts = new Dictionary<string, int>();

                foreach (var obj in pkg.Objects)
                {
                    if ((int)obj <= 0)
                        continue;

                    string className = obj.Class?.Name?.ToString() ?? "Unknown";

                    classCounts.TryGetValue(className, out int c);
                    classCounts[className] = c + 1;

                    try
                    {
                        var entry = ExtractObject(obj, className);
                        objects.Add(entry);
                        mapObjects++;
                    }
                    catch (Exception ex)
                    {
                        Console.Error.WriteLine($"  ERROR: {obj.Name} ({className}): {ex.Message}");
                        mapErrors++;
                    }
                }

                Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);

                var doc = new
                {
                    map = mapName,
                    path = mapPath,
                    object_count = objects.Count,
                    class_counts = classCounts
                        .OrderByDescending(kv => kv.Value)
                        .ToDictionary(kv => kv.Key, kv => kv.Value),
                    objects,
                };

                var jsonOpts = new JsonSerializerOptions
                {
                    WriteIndented = true,
                    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                };
                var json = JsonSerializer.Serialize(doc, jsonOpts);
                File.WriteAllText(outputPath, json);

                Console.WriteLine($"  Extracted {mapObjects} objects to: {outputPath}");
                if (mapErrors > 0)
                    Console.WriteLine($"  ({mapErrors} errors)");

                totalMaps++;
                totalErrors += mapErrors;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"FATAL: {mapPath}: {ex.Message}");
                totalErrors++;
            }
        }

        Console.WriteLine($"Total: {totalMaps} maps, {totalErrors} errors");
        return totalErrors > 0 ? 1 : 0;
    }

    private static JsonElement ExtractObject(UObject obj, string className)
    {
        // Load the object's serialized data
        obj.Load<UObjectRecordStream>();

        var properties = new List<Dictionary<string, string?>>();

        if (obj.Properties != null)
        {
            foreach (var prop in obj.Properties)
            {
                string? value = null;
                try
                {
                    value = prop.Decompile();
                }
                catch (Exception ex)
                {
                    value = $"/* ERROR: {ex.GetType().Name}: {ex.Message} */";
                }

                properties.Add(new Dictionary<string, string?>
                {
                    ["name"] = prop.Name?.ToString(),
                    ["type"] = prop.Type.ToString(),
                    ["size"] = prop.Size.ToString(),
                    ["array_index"] = prop.ArrayIndex.ToString(),
                    ["value"] = value,
                });
            }
        }

        var entry = new
        {
            name = obj.Name.ToString(),
            @class = className,
            outer = obj.Outer?.Name?.ToString(),
            properties,
        };

        var jsonOpts = new JsonSerializerOptions { WriteIndented = false };
        var json = JsonSerializer.Serialize(entry, jsonOpts);
        return JsonSerializer.Deserialize<JsonElement>(json);
    }

    private static void RegisterArrayTypeHints()
    {
        // Hints for arrays commonly found in map actors
        var hints = new (string name, string typeName, PropertyType type)[]
        {
            // WorldInfo
            ("NavigationPointList", "", PropertyType.ObjectProperty),
            ("PawnList", "", PropertyType.ObjectProperty),
            ("ControllerList", "", PropertyType.ObjectProperty),
            ("DynamicActorList", "", PropertyType.ObjectProperty),
            ("LightList", "", PropertyType.ObjectProperty),
            // ReachSpec
            ("StartEdge", "", PropertyType.ObjectProperty),
            ("EndEdge", "", PropertyType.ObjectProperty),
            // PlayerStart
            ("PlayerStart", "", PropertyType.ObjectProperty),
            // StaticMeshComponent
            ("StaticMesh", "", PropertyType.ObjectProperty),
            // SkeletalMeshComponent
            ("SkeletalMesh", "", PropertyType.ObjectProperty),
            ("Animations", "", PropertyType.ObjectProperty),
            // Brush/Model
            ("Polys", "", PropertyType.ObjectProperty),
            ("Elements", "", PropertyType.ObjectProperty),
            ("BrushComponent", "", PropertyType.ObjectProperty),
            // Lights
            ("LightComponent", "", PropertyType.ObjectProperty),
            // Kismet
            ("SequenceObjects", "", PropertyType.ObjectProperty),
            ("OutputLinks", "SeqOpOutputInputLink", PropertyType.StructProperty),
            ("InputLinks", "SeqOpOutputInputLink", PropertyType.StructProperty),
            ("VariableLinks", "SeqVarLink", PropertyType.StructProperty),
            ("LinkedVariables", "", PropertyType.ObjectProperty),
            // Audio
            ("SoundCue", "", PropertyType.ObjectProperty),
            // Terrain
            ("Layers", "TerrainLayer", PropertyType.StructProperty),
            ("DecoLayers", "DecoLayer", PropertyType.StructProperty),
            // Volume
            ("Components", "", PropertyType.ObjectProperty),
            // PathNode
            ("PathList", "ReachSpec", PropertyType.StructProperty),
            // Camera
            ("CameraComponent", "", PropertyType.ObjectProperty),
            // MeshComponent
            ("Materials", "", PropertyType.ObjectProperty),
            // StaticMeshComponent
            ("LODData", "StaticMeshComponentLODInfo", PropertyType.StructProperty),
            ("ShadowMaps", "", PropertyType.ObjectProperty),
            ("ShadowVertexBuffers", "", PropertyType.ObjectProperty),
            ("IrrelevantLights", "Guid", PropertyType.StructProperty),
            // WorldInfo
            ("StreamingLevels", "", PropertyType.ObjectProperty),
            ("GameTypesSupportedOnThisMap", "", PropertyType.ObjectProperty),
            ("ClientDestroyedActorContent", "", PropertyType.ObjectProperty),
            // TrMapInfo
            ("m_BEInvalidDeployableVolumes", "", PropertyType.ObjectProperty),
            ("m_DSInvalidDeployableVolumes", "", PropertyType.ObjectProperty),
            ("m_NeutralInvalidDeployableVolumes", "", PropertyType.ObjectProperty),
            // Sequence
            ("FindNamedObjects", "", PropertyType.ObjectProperty),
            ("LinkedOps", "", PropertyType.ObjectProperty),
            // Terrain
            ("WeightedMaterials", "", PropertyType.ObjectProperty),
            ("DecoLayers", "DecoLayer", PropertyType.StructProperty),
            // SoundNodeWave data
            ("RawData", "", PropertyType.ByteProperty),
        };

        foreach (var (name, typeName, type) in hints)
        {
            UnrealConfig.VariableTypes.TryAdd(name,
                new Tuple<string, PropertyType>(typeName, type));
        }
    }
}
