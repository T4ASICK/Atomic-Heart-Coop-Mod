using UnrealBuildTool;
using System.Collections.Generic;

public class AHGhostCoopLogicTarget : TargetRules
{
    public AHGhostCoopLogicTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V2;
        ExtraModuleNames.Add("AHGhostCoopLogic");
    }
}
