using UELib;
using UELib.Core;
using UELib.Types;
using Decompiler;

if (args.Length == 0)
{
    Console.Error.WriteLine("Usage: Decompiler <package.u> [output-dir]");
    Console.Error.WriteLine("       Decompiler gen [<package.u> <output-dir> ...]");
    Console.Error.WriteLine("       Decompiler assets [<package.u> <output.json> ...]");
    return 1;
}

// Asset extraction mode: "assets <pkg.u> <out.json> ..."
if (args[0] == "assets")
{
    return AssetExtractor.Run(args[1..]);
}

// Map extraction mode: "maps <map.fmap> <out.json> ..." or "maps --dir <dir> <outdir>"
if (args[0] == "maps")
{
    return MapExtractor.Run(args[1..]);
}

// Terrain dump mode: "terrain-dump <map.fmap> <output-dir>"
if (args[0] == "terrain-dump")
{
    return TerrainDumper.Run(args[1..]);
}

// Terrain extract mode: "terrain-extract <map.fmap> <output-dir>"
if (args[0] == "terrain-extract")
{
    return TerrainExtractor.Run(args[1..]);
}

// Single-package mode (backwards compatible)
if (args[0] != "gen")
{
    var packagePath = args[0];
    var outputDir = args.Length > 1
        ? args[1]
        : Path.Combine("gen", Path.GetFileNameWithoutExtension(packagePath));

    if (!File.Exists(packagePath))
    {
        Console.Error.WriteLine($"File not found: {packagePath}");
        return 1;
    }

    // Build NTL from the package itself (single-package mode)
    var ntlPkg = BuildNativesTable(packagePath);
    DecompilePackage(packagePath, outputDir, ntlPkg);
    return 0;
}

// Multi-package mode: "gen <pkg1.u> <out1> <pkg2.u> <out2> ..."
var remaining = args[1..];
if (remaining.Length == 0 || remaining.Length % 2 != 0)
{
    Console.Error.WriteLine("Usage: Decompiler gen [<package.u> <output-dir> ...]");
    return 1;
}

var packagePairs = new List<(string path, string outDir)>();
for (int i = 0; i < remaining.Length; i += 2)
{
    var path = remaining[i];
    var outDir = remaining[i + 1];
    if (!File.Exists(path))
    {
        Console.Error.WriteLine($"File not found: {path}");
        return 1;
    }
    packagePairs.Add((path, outDir));
}

// Phase 0: Build Natives Table from standard UE3 operator indices
// plus any native functions found in the game packages.
// The standard UE3 operator table is compiled into the engine binary
// (TribesAscend.exe) and not present in cooked .u files.
NativesTablePackage ntlPackage = UE3NativesTable.Create();
Console.WriteLine($"Loaded UE3 standard natives table: {ntlPackage.NativeTokenMap.Count} entries");

foreach (var (path, _) in packagePairs)
{
    var extra = BuildNativesTable(path);
    foreach (var item in extra.NativeTableList)
    {
        if (!ntlPackage.NativeTokenMap.ContainsKey((ushort)item.ByteToken))
        {
            ntlPackage.NativeTableList.Add(item);
            ntlPackage.NativeTokenMap[(ushort)item.ByteToken] = item;
        }
    }
}
Console.WriteLine($"Total natives after scanning packages: {ntlPackage.NativeTokenMap.Count} entries");

// Phase 1: Load and initialize all packages
var packages = new List<(string path, string outDir, UnrealPackage pkg)>();
foreach (var (path, outDir) in packagePairs)
{
    Console.WriteLine($"Loading: {path}");
    var pkg = UnrealLoader.LoadPackage(path);
    pkg.NTLPackage = ntlPackage ?? UE3NativesTable.Create();
    Console.WriteLine($"  Version: {pkg.Summary.Version}, Build: {pkg.Build}");

    Console.WriteLine("Initializing package...");
    pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);
    packages.Add((path, outDir, pkg));
}

// Phase 2: Decompile each package in dependency order, populating VariableTypes progressively
if (UnrealConfig.VariableTypes == null)
    UnrealConfig.VariableTypes = new();

int totalEmitted = 0;
int totalErrors = 0;

foreach (var (_, outDir, pkg) in packages)
{
    var classes = new List<UClass>();
    foreach (var obj in pkg.Objects)
    {
        if (obj is UClass cls && (int)obj > 0)
        {
            classes.Add(cls);
        }
    }

    Console.WriteLine($"Found {classes.Count} export classes in {Path.GetFileName(outDir)}.");
    Directory.CreateDirectory(outDir);

    var emitted = 0;
    var errors = 0;

    foreach (var cls in classes)
    {
        var filePath = Path.Combine(outDir, $"{cls.Name}.uc");
        try
        {
            cls.Load<UObjectRecordStream>();
            var source = cls.Decompile();
            File.WriteAllText(filePath, source);
            emitted++;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"  ERROR: {cls.Name}: {ex.Message}");
            try
            {
                File.WriteAllText(filePath,
                    $"// ERROR: Failed to decompile {cls.Name}: {ex.Message}\r\n" +
                    $"// This class may rely on imports or contain unsupported serialization.\r\n");
            }
            catch { }
            errors++;
        }
    }

    // Register this package's array types for cross-package resolution
    int typesBefore = UnrealConfig.VariableTypes.Count;
    foreach (var cls in classes)
    {
        RegisterArrayTypes(cls);
    }
    int typesAdded = UnrealConfig.VariableTypes.Count - typesBefore;

    Console.WriteLine($"Decompiled {emitted} classes to: {outDir} (+{typesAdded} array types)");
    if (errors > 0)
        Console.WriteLine($"  ({errors} errors)");
    totalEmitted += emitted;
    totalErrors += errors;
}

Console.WriteLine($"Total: {totalEmitted} classes, {totalErrors} errors across {packages.Count} packages ({UnrealConfig.VariableTypes.Count} array types).");
return totalErrors > 0 ? 1 : 0;

// Build a Natives Table from a package's native functions.
// This maps native function indices (e.g., 119) to their operator names (e.g., "!=").
// Without this, all operators in decompiled bytecode appear as __NFUN_XXX__.
static NativesTablePackage BuildNativesTable(string packagePath)
{
    var ntl = new NativesTablePackage();
    try
    {
        Console.WriteLine($"Scanning for native functions in {Path.GetFileName(packagePath)}...");
        var pkg = UnrealLoader.LoadPackage(packagePath);
        pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);

        var nativeItems = new List<NativeTableItem>();
        foreach (var obj in pkg.Objects)
        {
            if (obj is UFunction func)
            {
                try { func.Load<UObjectRecordStream>(); } catch { }
                if (func.NativeToken > 0)
                {
                    var item = new NativeTableItem(func);
                    if (!nativeItems.Any(n => n.ByteToken == item.ByteToken))
                    {
                        nativeItems.Add(item);
                    }
                }
            }
        }

        ntl.NativeTableList = nativeItems;
        ntl.NativeTokenMap = nativeItems.ToDictionary(item => (ushort)item.ByteToken);
        Console.WriteLine($"  Found {ntl.NativeTokenMap.Count} native functions");
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"  WARNING: Failed to build NTL from {packagePath}: {ex.Message}");
        ntl.NativeTableList = new();
        ntl.NativeTokenMap = new();
    }
    return ntl;
}

// Single-package helper
static void DecompilePackage(string packagePath, string outputDir, NativesTablePackage? ntlPackage = null)
{
    Console.WriteLine($"Loading: {packagePath}");
    var pkg = UnrealLoader.LoadPackage(packagePath);
    if (ntlPackage != null)
        pkg.NTLPackage = ntlPackage;
    Console.WriteLine($"  Version: {pkg.Summary.Version}, Build: {pkg.Build}");

    Console.WriteLine("Initializing package...");
    pkg.InitializePackage(UnrealPackage.InitFlags.Construct | UnrealPackage.InitFlags.RegisterClasses);

    var classes = new List<UClass>();
    foreach (var obj in pkg.Objects)
    {
        if (obj is UClass cls && (int)obj > 0)
        {
            classes.Add(cls);
        }
    }

    Console.WriteLine($"Found {classes.Count} export classes.");
    Directory.CreateDirectory(outputDir);

    var emitted = 0;
    var errors = 0;

    foreach (var cls in classes)
    {
        var filePath = Path.Combine(outputDir, $"{cls.Name}.uc");
        try
        {
            cls.Load<UObjectRecordStream>();
            var source = cls.Decompile();
            File.WriteAllText(filePath, source);
            emitted++;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"  ERROR: {cls.Name}: {ex.Message}");
            try
            {
                File.WriteAllText(filePath,
                    $"// ERROR: Failed to decompile {cls.Name}: {ex.Message}\r\n" +
                    $"// This class may rely on imports or contain unsupported serialization.\r\n");
            }
            catch { }
            errors++;
        }
    }

    Console.WriteLine($"Decompiled {emitted} classes to: {outputDir}");
    if (errors > 0)
        Console.WriteLine($"  ({errors} errors)");
}

static void RegisterArrayTypes(UELib.Core.UStruct structObj)
{
    foreach (var field in structObj.EnumerateFields())
    {
        if (field is UELib.Core.UArrayProperty arrayProp && arrayProp.InnerProperty != null)
        {
            // For struct arrays, store the struct name as the first tuple element.
            // For byte arrays with an enum, store the enum name.
            string extraInfo = arrayProp.InnerProperty.Type.ToString();
            if (arrayProp.InnerProperty is UELib.Core.UStructProperty structProp && structProp.Struct != null)
            {
                extraInfo = structProp.Struct.Name;
                UnrealConfig.ArrayStructTypes ??= new();
                UnrealConfig.ArrayStructTypes.TryAdd(arrayProp.Name, structProp.Struct);
            }
            else if (arrayProp.InnerProperty is UELib.Core.UByteProperty byteProp && byteProp.Enum != null)
                extraInfo = byteProp.Enum.Name;

            UnrealConfig.VariableTypes.TryAdd(arrayProp.Name,
                new Tuple<string, PropertyType>(extraInfo, arrayProp.InnerProperty.Type));
        }
        // Recurse into nested ScriptStructs to register their array types too
        if (field is UELib.Core.UScriptStruct nestedStruct)
        {
            RegisterArrayTypes(nestedStruct);
        }
    }
}