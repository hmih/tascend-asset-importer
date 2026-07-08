using System.Text.Json;
using System.Text.Json.Serialization;
using UELib;
using UELib.Core;
using UELib.Types;

namespace Decompiler;

/// <summary>
/// Extracts property data for non-mesh asset objects (ParticleSystem, PhysicsAsset,
/// AnimTree, Font) from UE3 .u packages and dumps them to JSON.
///
/// These object types are not supported by UModel's exporter, so we use UELib's
/// property deserializer to read their structured data directly.
/// </summary>
public static class AssetExtractor
{
    private static readonly HashSet<string> TargetClasses = new(StringComparer.OrdinalIgnoreCase)
    {
        "ParticleSystem",
        "PhysicsAsset",
        "AnimTree",
        "Font",
    };

    public static int Run(string[] args)
    {
        if (args.Length == 0 || (args.Length == 1 && args[0] == "--help"))
        {
            Console.Error.WriteLine("Usage: Decompiler assets <package.u> <output.json> [<package.u> <output.json> ...]");
            return 1;
        }

        if (args.Length % 2 != 0)
        {
            Console.Error.WriteLine("Expected pairs of <package.u> <output.json>");
            return 1;
        }

        // Initialize the decompiling state required by UELib's property decompiler.
        if (UnrealConfig.VariableTypes == null)
            UnrealConfig.VariableTypes = new();
        RegisterArrayTypeHints();

        int totalObjects = 0;
        int totalErrors = 0;

        for (int i = 0; i < args.Length; i += 2)
        {
            var packagePath = args[i];
            var outputPath = args[i + 1];

            if (!File.Exists(packagePath))
            {
                Console.Error.WriteLine($"File not found: {packagePath}");
                return 1;
            }

            Console.WriteLine($"Loading: {packagePath}");
            var pkg = UnrealLoader.LoadPackage(packagePath);
            pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);
            Console.WriteLine($"  Version: {pkg.Summary.Version}, Objects: {pkg.Objects.Count}");

            var packageName = Path.GetFileNameWithoutExtension(packagePath);
            var objects = new List<JsonElement>();
            int pkgObjects = 0;
            int pkgErrors = 0;

            foreach (var obj in pkg.Objects)
            {
                // Only exports (positive index)
                if ((int)obj <= 0)
                    continue;

                string className = obj.Class?.Name?.ToString() ?? "Unknown";
                if (!TargetClasses.Contains(className))
                    continue;

                try
                {
                    var entry = ExtractObject(obj, packageName, className);
                    objects.Add(entry);
                    pkgObjects++;
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine($"  ERROR: {obj.Name} ({className}): {ex.Message}");
                    pkgErrors++;
                }
            }

            // Collect subobjects too (emitters, modules, etc.) — they are exports
            // that have an outer which is one of our target objects.
            var targetObjectNames = new HashSet<string>(
                objects.Select(o => o.GetProperty("name").GetString()!),
                StringComparer.OrdinalIgnoreCase
            );

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);

            var doc = new
            {
                package = packageName,
                object_count = objects.Count,
                objects,
            };

            var jsonOpts = new JsonSerializerOptions
            {
                WriteIndented = true,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            };
            var json = JsonSerializer.Serialize(doc, jsonOpts);
            File.WriteAllText(outputPath, json);

            Console.WriteLine($"  Extracted {pkgObjects} objects to: {outputPath}");
            if (pkgErrors > 0)
                Console.WriteLine($"  ({pkgErrors} errors)");

            totalObjects += pkgObjects;
            totalErrors += pkgErrors;
        }

        Console.WriteLine($"Total: {totalObjects} objects, {totalErrors} errors");
        return totalErrors > 0 ? 1 : 0;
    }

    private static JsonElement ExtractObject(UObject obj, string packageName, string className)
    {
        Console.WriteLine($"  Extracting: {className} {obj.Name}");

        // Load the object's serialized data, which populates Properties
        obj.Load<UObjectRecordStream>();

        var properties = new List<Dictionary<string, string?>>();
        if (obj.Properties != null)
        {
            foreach (var prop in obj.Properties)
            {
                string? value = null;
                try
                {
                    // Decompile() returns the full t3d-formatted value,
                    // including inlined subobjects for object/array properties.
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
            package = packageName,
            path = obj.GetPath(),
            outer = obj.Outer?.Name?.ToString(),
            properties,
        };

        // Serialize to JSON, then parse back as JsonElement for nesting
        var jsonOpts = new JsonSerializerOptions { WriteIndented = false };
        var json = JsonSerializer.Serialize(entry, jsonOpts);
        return JsonSerializer.Deserialize<JsonElement>(json);
    }

    /// <summary>
    /// Register array element type hints for properties in target classes.
    /// UELib's array deserializer needs to know the inner type of each array
    /// property, but since these classes are deserialized as UnknownObject
    /// (no typed class), FindProperty fails. We provide the type info directly.
    /// </summary>
    private static void RegisterArrayTypeHints()
    {
        var hints = new (string name, string typeName, PropertyType type)[]
        {
            // ParticleSystem
            ("Emitters", "", PropertyType.ObjectProperty),
            ("LODDistances", "", PropertyType.FloatProperty),
            ("LODSettings", "ParticleSystemLOD", PropertyType.StructProperty),
            ("SoloTracking", "LODSoloTrack", PropertyType.StructProperty),

            // ParticleEmitter
            ("LODLevels", "", PropertyType.ObjectProperty),

            // ParticleLODLevel
            ("Modules", "", PropertyType.ObjectProperty),
            ("SpawningModules", "", PropertyType.ObjectProperty),
            ("SpawnModules", "", PropertyType.ObjectProperty),
            ("UpdateModules", "", PropertyType.ObjectProperty),
            ("OrbitModules", "", PropertyType.ObjectProperty),
            ("EventReceiverModules", "", PropertyType.ObjectProperty),

            // ParticleModule
            ("RandomSeeds", "", PropertyType.IntProperty),

            // ParticleModuleRequired / ParticleModuleSpawn
            ("BurstList", "ParticleBurst", PropertyType.StructProperty),

            // PhysicsAsset
            ("BodySetup", "", PropertyType.ObjectProperty),
            ("BoundsBodies", "", PropertyType.IntProperty),
            ("ConstraintSetup", "", PropertyType.ObjectProperty),

            // PhysicsAssetInstance
            ("Bodies", "", PropertyType.ObjectProperty),
            ("Constraints", "", PropertyType.ObjectProperty),

            // RB_BodySetup
            ("CachedConvexElements", "KCachedConvexDataElement", PropertyType.StructProperty),
            ("CollisionGeom", "", PropertyType.PointerProperty),
            ("CollisionGeomScale3D", "Vector", PropertyType.StructProperty),
            ("PreCachedPhysScale", "Vector", PropertyType.StructProperty),
            ("PreCachedPhysData", "", PropertyType.ObjectProperty),
            ("ConvexElementData", "", PropertyType.ByteProperty),

            // AnimTree
            ("SeqNodes", "", PropertyType.ObjectProperty),
            ("PreviewMorphSets", "", PropertyType.ObjectProperty),
            ("PreviewAnimSets", "", PropertyType.ObjectProperty),
            ("AnimGroups", "AnimGroup", PropertyType.StructProperty),
            ("PrioritizedSkelBranches", "", PropertyType.NameProperty),
            ("ComposePrePassBoneNames", "", PropertyType.NameProperty),
            ("ComposePostPassBoneNames", "", PropertyType.NameProperty),
            ("RootMorphNodes", "", PropertyType.ObjectProperty),
            ("SkelControlLists", "", PropertyType.ObjectProperty),
            ("SavedPose", "BoneAtom", PropertyType.StructProperty),
            ("PreviewMeshList", "PreviewSkelMeshStruct", PropertyType.StructProperty),
            ("PreviewSocketList", "PreviewSocketStruct", PropertyType.StructProperty),
            ("PreviewAnimSetList", "PreviewAnimSetsStruct", PropertyType.StructProperty),
            ("AnimTickArray", "", PropertyType.ObjectProperty),

            // Font
            ("Characters", "FontCharacter", PropertyType.StructProperty),
            ("Textures", "", PropertyType.ObjectProperty),
            ("MaxCharHeight", "", PropertyType.IntProperty),
        };

        foreach (var (name, typeName, type) in hints)
        {
            UnrealConfig.VariableTypes.TryAdd(name,
                new Tuple<string, PropertyType>(typeName, type));
        }
    }
}
