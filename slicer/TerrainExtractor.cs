using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using UELib;
using UELib.Core;

namespace Decompiler;

/// <summary>
/// Extracts terrain geometry from UE3 .fmap files by reading pre-computed
/// world-space vertices from TerrainComponent objects.
///
/// Each TerrainComponent stores:
///   - Properties (SectionSizeX/Y, TrueSectionSizeX/Y, ShadowMaps, etc.)
///   - Trailing binary: u32 vertex_count, then count × FVector (3 × float32)
///
/// The vertex count is (TrueSectionSizeX+1) × (TrueSectionSizeY+1).
/// Common values: SectionSize=16 → 17×17=289, SectionSize=32 → 33×33=1089,
/// SectionSize=128 → 129×129=16641.
///
/// The grid is row-major: vertex(row, col) = row * (TrueSectionSizeX+1) + col.
/// Vertices are already in world space (Z-up, left-handed UE3 coordinates).
/// </summary>
public static class TerrainExtractor
{
    public static int Run(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("Usage: Decompiler terrain-extract <map.fmap> <output-dir>");
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

        var allVertices = new List<float[]>();
        var allIndices = new List<int>();
        int componentCount = 0;
        int skipped = 0;

        foreach (var obj in pkg.Objects)
        {
            if ((int)obj <= 0) continue;
            string className = obj.Class?.Name?.ToString() ?? "Unknown";
            if (className != "TerrainComponent") continue;

            try
            {
                obj.Load<UObjectRecordStream>();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"  Failed to load {obj.Name}: {ex.Message}");
                continue;
            }

            var buffer = obj.CopyBuffer();
            if (buffer == null) continue;

            // Read grid dimensions from properties
            int trueSizeX = 16, trueSizeY = 16; // defaults
            if (obj.Properties != null)
            {
                foreach (var prop in obj.Properties)
                {
                    string pname = prop.Name?.ToString() ?? "";
                    if (pname == "TrueSectionSizeX")
                        trueSizeX = ParsePropertyValue(prop);
                    else if (pname == "TrueSectionSizeY")
                        trueSizeY = ParsePropertyValue(prop);
                }
            }

            int gridW = trueSizeX + 1;
            int gridH = trueSizeY + 1;
            int vertexCount = gridW * gridH;

            // Search for the vertex count value in the buffer
            int countOffset = -1;
            for (int i = 0; i <= buffer.Length - 4 - vertexCount * 12; i++)
            {
                if (BitConverter.ToInt32(buffer, i) != vertexCount) continue;

                int dataStart = i + 4;

                // Validate ALL vertices: must be finite and within ±500000
                bool valid = true;
                for (int j = 0; j < vertexCount && valid; j++)
                {
                    float x = BitConverter.ToSingle(buffer, dataStart + j * 12);
                    float y = BitConverter.ToSingle(buffer, dataStart + j * 12 + 4);
                    float z = BitConverter.ToSingle(buffer, dataStart + j * 12 + 8);
                    if (!float.IsFinite(x) || !float.IsFinite(y) || !float.IsFinite(z) ||
                        Math.Abs(x) > 500000 || Math.Abs(y) > 500000 || Math.Abs(z) > 500000)
                    {
                        valid = false;
                    }
                }
                if (!valid) continue;

                // Grid structure check: vertices in the same row should share Y
                // (or be very close), and adjacent rows should have different Y.
                for (int row = 0; row < gridH && valid; row++)
                {
                    float rowY0 = BitConverter.ToSingle(buffer, dataStart + (row * gridW) * 12 + 4);
                    for (int col = 1; col < gridW && valid; col++)
                    {
                        float vy = BitConverter.ToSingle(buffer, dataStart + (row * gridW + col) * 12 + 4);
                        if (Math.Abs(vy - rowY0) > 2.0f) valid = false;
                    }
                }
                if (!valid) continue;

                countOffset = i;
                break;
            }

            if (countOffset < 0)
            {
                Console.Error.WriteLine($"  Could not find vertex data in {obj.Name} (expected count={vertexCount}, grid={gridW}×{gridH})");
                skipped++;
                continue;
            }

            // Read vertices
            int baseVertex = allVertices.Count;
            int dataOffset = countOffset + 4;
            for (int i = 0; i < vertexCount; i++)
            {
                float x = BitConverter.ToSingle(buffer, dataOffset + i * 12);
                float y = BitConverter.ToSingle(buffer, dataOffset + i * 12 + 4);
                float z = BitConverter.ToSingle(buffer, dataOffset + i * 12 + 8);
                allVertices.Add(new float[] { x, y, z });
            }

            // Generate indices for this grid: (gridW-1) × (gridH-1) quads, 2 triangles each
            // Grid is row-major: vertex(row, col) = row * gridW + col
            for (int row = 0; row < gridH - 1; row++)
            {
                for (int col = 0; col < gridW - 1; col++)
                {
                    int v00 = baseVertex + row * gridW + col;
                    int v10 = baseVertex + row * gridW + (col + 1);
                    int v01 = baseVertex + (row + 1) * gridW + col;
                    int v11 = baseVertex + (row + 1) * gridW + (col + 1);
                    allIndices.Add(v00);
                    allIndices.Add(v01);
                    allIndices.Add(v10);
                    allIndices.Add(v10);
                    allIndices.Add(v01);
                    allIndices.Add(v11);
                }
            }

            componentCount++;
        }

        if (componentCount == 0)
        {
            Console.Error.WriteLine("  No TerrainComponent objects with valid vertex data found");
            return 1;
        }

        Console.WriteLine($"  Extracted {componentCount} TerrainComponents ({skipped} skipped)");
        Console.WriteLine($"  Total vertices: {allVertices.Count}");
        Console.WriteLine($"  Total triangles: {allIndices.Count / 3}");

        // Compute bounds
        float minX = float.MaxValue, maxX = float.MinValue;
        float minY = float.MaxValue, maxY = float.MinValue;
        float minZ = float.MaxValue, maxZ = float.MinValue;
        foreach (var v in allVertices)
        {
            if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
            if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
            if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
        }
        Console.WriteLine($"  Bounds: X[{minX:F1}, {maxX:F1}] Y[{minY:F1}, {maxY:F1}] Z[{minZ:F1}, {maxZ:F1}]");

        // Write raw vertex data (binary: vertices as float32 triples, indices as uint32)
        var mapName = Path.GetFileNameWithoutExtension(mapPath);
        var rawPath = Path.Combine(outputDir, $"{mapName}.terrain.bin");
        using (var bw = new BinaryWriter(File.OpenWrite(rawPath)))
        {
            bw.Write(allVertices.Count);
            foreach (var v in allVertices)
            {
                bw.Write(v[0]); bw.Write(v[1]); bw.Write(v[2]);
            }
            bw.Write(allIndices.Count);
            foreach (var idx in allIndices) bw.Write(idx);
        }
        Console.WriteLine($"  Wrote: {rawPath} ({4 + allVertices.Count * 12 + 4 + allIndices.Count * 4} bytes)");

        // Write metadata JSON
        var meta = new
        {
            map = mapName,
            component_count = componentCount,
            vertex_count = allVertices.Count,
            triangle_count = allIndices.Count / 3,
            coordinate_system = "UE3 (left-handed, Z-up: X=forward, Y=right, Z=up)",
            bounds = new
            {
                min_x = minX, max_x = maxX,
                min_y = minY, max_y = maxY,
                min_z = minZ, max_z = maxZ,
            },
        };
        var metaPath = Path.Combine(outputDir, $"{mapName}.terrain.json");
        var jsonOpts = new JsonSerializerOptions { WriteIndented = true };
        File.WriteAllText(metaPath, JsonSerializer.Serialize(meta, jsonOpts));
        Console.WriteLine($"  Wrote: {metaPath}");

        return 0;
    }

    /// <summary>
    /// Parse an integer value from a UELib UDefaultProperty.
    /// Uses Decompile() and parses the resulting string.
    /// </summary>
    private static int ParsePropertyValue(UDefaultProperty prop)
    {
        try
        {
            string s = prop.Decompile();
            // Format is typically "Name=123"
            int eq = s.IndexOf('=');
            if (eq >= 0)
                s = s.Substring(eq + 1).Trim();
            if (int.TryParse(s, out int val))
                return val;
        }
        catch { }
        return 16; // default
    }
}
