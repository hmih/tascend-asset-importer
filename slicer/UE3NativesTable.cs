using UELib;
using UELib.Core;

namespace Decompiler;

/// <summary>
/// Standard UE3 native function/operator table.
/// Indices extracted from decompiled Object.uc (and Actor.uc for class-specific natives).
/// These are the native token values used in UE3 bytecode.
/// </summary>
public static class UE3NativesTable
{
    public static NativesTablePackage Create()
    {
        var items = new List<NativeTableItem>();

        // String operators
        Add(items, 112, "$", FunctionType.Operator, 40);
        Add(items, 168, "@", FunctionType.Operator, 40);

        // Bool operators
        Add(items, 114, "==", FunctionType.Operator, 24);
        Add(items, 119, "!=", FunctionType.Operator, 26);
        Add(items, 122, "==", FunctionType.Operator, 24);
        Add(items, 123, "!=", FunctionType.Operator, 26);
        Add(items, 129, "!", FunctionType.PreOperator, 0);
        Add(items, 130, "&&", FunctionType.Operator, 30);
        Add(items, 131, "^^", FunctionType.Operator, 30);
        Add(items, 132, "||", FunctionType.Operator, 32);
        Add(items, 142, "==", FunctionType.Operator, 24);
        Add(items, 154, "==", FunctionType.Operator, 24);
        Add(items, 155, "!=", FunctionType.Operator, 26);
        Add(items, 180, "==", FunctionType.Operator, 24);
        Add(items, 181, "!=", FunctionType.Operator, 26);
        Add(items, 203, "!=", FunctionType.Operator, 26);
        Add(items, 217, "==", FunctionType.Operator, 24);
        Add(items, 218, "!=", FunctionType.Operator, 26);
        Add(items, 242, "==", FunctionType.Operator, 24);
        Add(items, 243, "!=", FunctionType.Operator, 26);
        Add(items, 254, "==", FunctionType.Operator, 24);
        Add(items, 255, "!=", FunctionType.Operator, 26);

        // Int operators
        Add(items, 115, "<", FunctionType.Operator, 24);
        Add(items, 116, ">", FunctionType.Operator, 24);
        Add(items, 120, "<=", FunctionType.Operator, 24);
        Add(items, 121, ">=", FunctionType.Operator, 24);
        Add(items, 124, "~=", FunctionType.Operator, 24);
        Add(items, 133, "*=", FunctionType.Operator, 34);
        Add(items, 134, "/=", FunctionType.Operator, 34);
        Add(items, 135, "+=", FunctionType.Operator, 34);
        Add(items, 136, "-=", FunctionType.Operator, 34);
        Add(items, 137, "++", FunctionType.PreOperator, 0);
        Add(items, 138, "--", FunctionType.PreOperator, 0);
        Add(items, 139, "++", FunctionType.PostOperator, 0);
        Add(items, 140, "--", FunctionType.PostOperator, 0);
        Add(items, 141, "~", FunctionType.PreOperator, 0);
        Add(items, 143, "-", FunctionType.PreOperator, 0);
        Add(items, 144, "*", FunctionType.Operator, 16);
        Add(items, 145, "/", FunctionType.Operator, 16);
        Add(items, 146, "+", FunctionType.Operator, 20);
        Add(items, 147, "-", FunctionType.Operator, 20);
        Add(items, 148, "<<", FunctionType.Operator, 22);
        Add(items, 149, ">>", FunctionType.Operator, 22);
        Add(items, 150, "<", FunctionType.Operator, 24);
        Add(items, 151, ">", FunctionType.Operator, 24);
        Add(items, 152, "<=", FunctionType.Operator, 24);
        Add(items, 153, ">=", FunctionType.Operator, 24);
        Add(items, 156, "&", FunctionType.Operator, 28);
        Add(items, 157, "^", FunctionType.Operator, 28);
        Add(items, 158, "|", FunctionType.Operator, 28);
        Add(items, 159, "*=", FunctionType.Operator, 34);
        Add(items, 160, "/=", FunctionType.Operator, 34);
        Add(items, 161, "+=", FunctionType.Operator, 34);
        Add(items, 162, "-=", FunctionType.Operator, 34);
        Add(items, 163, "++", FunctionType.PreOperator, 0);
        Add(items, 164, "--", FunctionType.PreOperator, 0);
        Add(items, 165, "++", FunctionType.PostOperator, 0);
        Add(items, 166, "--", FunctionType.PostOperator, 0);

        // Float operators
        Add(items, 169, "-", FunctionType.PreOperator, 0);
        Add(items, 170, "**", FunctionType.Operator, 12);
        Add(items, 171, "*", FunctionType.Operator, 16);
        Add(items, 172, "/", FunctionType.Operator, 16);
        Add(items, 173, "%", FunctionType.Operator, 18);
        Add(items, 174, "+", FunctionType.Operator, 20);
        Add(items, 175, "-", FunctionType.Operator, 20);
        Add(items, 176, "<", FunctionType.Operator, 24);
        Add(items, 177, ">", FunctionType.Operator, 24);
        Add(items, 178, "<=", FunctionType.Operator, 24);
        Add(items, 179, ">=", FunctionType.Operator, 24);
        Add(items, 182, "*=", FunctionType.Operator, 34);
        Add(items, 183, "/=", FunctionType.Operator, 34);
        Add(items, 184, "+=", FunctionType.Operator, 34);
        Add(items, 185, "-=", FunctionType.Operator, 34);
        Add(items, 196, ">>>", FunctionType.Operator, 22);
        Add(items, 198, "*=", FunctionType.Operator, 34);

        // Vector operators
        Add(items, 210, "~=", FunctionType.Operator, 24);
        Add(items, 211, "-", FunctionType.PreOperator, 0);
        Add(items, 212, "*", FunctionType.Operator, 16);
        Add(items, 213, "*", FunctionType.Operator, 16);
        Add(items, 214, "/", FunctionType.Operator, 16);
        Add(items, 215, "+", FunctionType.Operator, 20);
        Add(items, 216, "-", FunctionType.Operator, 20);
        Add(items, 219, "Dot", FunctionType.Operator, 16);
        Add(items, 220, "Cross", FunctionType.Operator, 16);
        Add(items, 221, "*=", FunctionType.Operator, 34);
        Add(items, 222, "/=", FunctionType.Operator, 34);
        Add(items, 223, "+=", FunctionType.Operator, 34);
        Add(items, 224, "-=", FunctionType.Operator, 34);

        // Rotator operators
        Add(items, 253, "%", FunctionType.Operator, 18);
        Add(items, 270, "+", FunctionType.Operator, 16);
        Add(items, 271, "-", FunctionType.Operator, 16);
        Add(items, 275, "<<", FunctionType.Operator, 22);
        Add(items, 276, ">>", FunctionType.Operator, 22);
        Add(items, 287, "*", FunctionType.Operator, 16);
        Add(items, 288, "*", FunctionType.Operator, 16);
        Add(items, 289, "/", FunctionType.Operator, 16);
        Add(items, 290, "*=", FunctionType.Operator, 34);
        Add(items, 291, "/=", FunctionType.Operator, 34);
        Add(items, 296, "*", FunctionType.Operator, 16);
        Add(items, 297, "*=", FunctionType.Operator, 34);
        Add(items, 316, "+", FunctionType.Operator, 20);
        Add(items, 317, "-", FunctionType.Operator, 20);
        Add(items, 318, "+=", FunctionType.Operator, 34);
        Add(items, 319, "-=", FunctionType.Operator, 34);
        Add(items, 322, "$=", FunctionType.Operator, 44);
        Add(items, 323, "@=", FunctionType.Operator, 44);
        Add(items, 324, "-=", FunctionType.Operator, 45);

        // String functions
        Add(items, 125, "Len", FunctionType.Function, 0);
        Add(items, 126, "InStr", FunctionType.Function, 0);
        Add(items, 127, "Mid", FunctionType.Function, 0);
        Add(items, 128, "Left", FunctionType.Function, 0);
        Add(items, 201, "Repl", FunctionType.Function, 0);
        Add(items, 234, "Right", FunctionType.Function, 0);
        Add(items, 235, "Caps", FunctionType.Function, 0);
        Add(items, 236, "Chr", FunctionType.Function, 0);
        Add(items, 237, "Asc", FunctionType.Function, 0);
        Add(items, 238, "Locs", FunctionType.Function, 0);

        // Math functions
        Add(items, 167, "Rand", FunctionType.Function, 0);
        Add(items, 186, "Abs", FunctionType.Function, 0);
        Add(items, 187, "Sin", FunctionType.Function, 0);
        Add(items, 188, "Cos", FunctionType.Function, 0);
        Add(items, 189, "Tan", FunctionType.Function, 0);
        Add(items, 190, "Atan", FunctionType.Function, 0);
        Add(items, 191, "Exp", FunctionType.Function, 0);
        Add(items, 192, "Loge", FunctionType.Function, 0);
        Add(items, 193, "Sqrt", FunctionType.Function, 0);
        Add(items, 194, "Square", FunctionType.Function, 0);
        Add(items, 195, "FRand", FunctionType.Function, 0);
        Add(items, 199, "Round", FunctionType.Function, 0);
        Add(items, 244, "FMin", FunctionType.Function, 0);
        Add(items, 245, "FMax", FunctionType.Function, 0);
        Add(items, 246, "FClamp", FunctionType.Function, 0);
        Add(items, 247, "Lerp", FunctionType.Function, 0);
        Add(items, 249, "Min", FunctionType.Function, 0);
        Add(items, 250, "Max", FunctionType.Function, 0);
        Add(items, 251, "Clamp", FunctionType.Function, 0);
        Add(items, 252, "VRand", FunctionType.Function, 0);
        Add(items, 320, "RotRand", FunctionType.Function, 0);

        // Vector functions
        Add(items, 225, "VSize", FunctionType.Function, 0);
        Add(items, 226, "Normal", FunctionType.Function, 0);
        Add(items, 228, "VSizeSq", FunctionType.Function, 0);
        Add(items, 229, "GetAxes", FunctionType.Function, 0);
        Add(items, 230, "GetUnAxes", FunctionType.Function, 0);
        Add(items, 300, "MirrorVectorByNormal", FunctionType.Function, 0);
        Add(items, 1500, "ProjectOnTo", FunctionType.Function, 0);
        Add(items, 1501, "IsZero", FunctionType.Function, 0);

        // Object functions
        Add(items, 113, "GotoState", FunctionType.Function, 0);
        Add(items, 117, "Enable", FunctionType.Function, 0);
        Add(items, 118, "Disable", FunctionType.Function, 0);
        Add(items, 197, "IsA", FunctionType.Function, 0);
        Add(items, 231, "LogInternal", FunctionType.Function, 0);
        Add(items, 232, "WarnInternal", FunctionType.Function, 0);
        Add(items, 258, "ClassIsChildOf", FunctionType.Function, 0);
        Add(items, 281, "IsInState", FunctionType.Function, 0);
        Add(items, 284, "GetStateName", FunctionType.Function, 0);
        Add(items, 536, "SaveConfig", FunctionType.Function, 0);

        // Actor functions
        Add(items, 256, "Sleep", FunctionType.Function, 0);  // Actor.
        Add(items, 261, "FinishAnim", FunctionType.Function, 0);  // Actor.
        Add(items, 262, "SetCollision", FunctionType.Function, 0);  // Actor.
        Add(items, 266, "Move", FunctionType.Function, 0);  // Actor.
        Add(items, 267, "SetLocation", FunctionType.Function, 0);  // Actor.
        Add(items, 272, "SetOwner", FunctionType.Function, 0);  // Actor.
        Add(items, 277, "Trace", FunctionType.Function, 0);  // Actor.
        Add(items, 279, "Destroy", FunctionType.Function, 0);  // Actor.
        Add(items, 280, "SetTimer", FunctionType.Function, 0);  // Actor.
        Add(items, 283, "SetCollisionSize", FunctionType.Function, 0);  // Actor.
        Add(items, 298, "SetBase", FunctionType.Function, 0);  // Actor.
        Add(items, 299, "SetRotation", FunctionType.Function, 0);  // Actor.
        Add(items, 304, "AllActors", FunctionType.Function, 0);  // Actor.
        Add(items, 305, "ChildActors", FunctionType.Function, 0);  // Actor.
        Add(items, 306, "BasedActors", FunctionType.Function, 0);  // Actor.
        Add(items, 307, "TouchingActors", FunctionType.Function, 0);  // Actor.
        Add(items, 309, "TraceActors", FunctionType.Function, 0);  // Actor.
        Add(items, 311, "VisibleActors", FunctionType.Function, 0);  // Actor.
        Add(items, 312, "VisibleCollidingActors", FunctionType.Function, 0);  // Actor.
        Add(items, 313, "DynamicActors", FunctionType.Function, 0);  // Actor.
        Add(items, 321, "CollidingActors", FunctionType.Function, 0);  // Actor.
        Add(items, 512, "MakeNoise", FunctionType.Function, 0);  // Actor.
        Add(items, 532, "PlayerCanSeeMe", FunctionType.Function, 0);  // Actor.
        Add(items, 547, "GetURLMap", FunctionType.Function, 0);  // Actor.
        Add(items, 548, "FastTrace", FunctionType.Function, 0);  // Actor.
        Add(items, 3969, "MoveSmooth", FunctionType.Function, 0);  // Actor.
        Add(items, 3970, "SetPhysics", FunctionType.Function, 0);  // Actor.
        Add(items, 3971, "AutonomousPhysics", FunctionType.Function, 0);  // Actor.

        // Controller functions
        Add(items, 500, "MoveTo", FunctionType.Function, 0);  // Controller.
        Add(items, 502, "MoveToward", FunctionType.Function, 0);  // Controller.
        Add(items, 508, "FinishRotation", FunctionType.Function, 0);  // Controller.
        Add(items, 514, "LineOfSightTo", FunctionType.Function, 0);  // Controller.
        Add(items, 517, "FindPathToward", FunctionType.Function, 0);  // Controller.
        Add(items, 518, "FindPathTo", FunctionType.Function, 0);  // Controller.
        Add(items, 520, "ActorReachable", FunctionType.Function, 0);  // Controller.
        Add(items, 521, "PointReachable", FunctionType.Function, 0);  // Controller.
        Add(items, 525, "FindRandomDest", FunctionType.Function, 0);  // Controller.
        Add(items, 526, "PickWallAdjust", FunctionType.Function, 0);  // Controller.
        Add(items, 527, "WaitForLanding", FunctionType.Function, 0);  // Controller.
        Add(items, 531, "PickTarget", FunctionType.Function, 0);  // Controller.
        Add(items, 533, "CanSee", FunctionType.Function, 0);  // Controller.
        Add(items, 537, "CanSeeByPoints", FunctionType.Function, 0);  // Controller.

        // Other
        Add(items, 524, "FindStairRotation", FunctionType.Function, 0);  // PlayerController.
        Add(items, 546, "UpdateURL", FunctionType.Function, 0);  // PlayerController.
        Add(items, 999, "IsSeatControllerReplicationViewer", FunctionType.Function, 0);  // UDKVehicle.

        // Compose into NativesTablePackage
        var ntl = new NativesTablePackage
        {
            NativeTableList = items,
            NativeTokenMap = items.ToDictionary(item => (ushort)item.ByteToken),
        };
        return ntl;
    }

    private static void Add(List<NativeTableItem> items, int token, string name,
        FunctionType type, byte precedence)
    {
        items.Add(new NativeTableItem
        {
            Name = name,
            ByteToken = token,
            Type = type,
            OperPrecedence = precedence,
        });
    }
}
