using UnrealBuildTool;
using System.Collections.Generic;

public class AHGhostCoopLogicEditorTarget : TargetRules
{
    public AHGhostCoopLogicEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V2;
        ExtraModuleNames.AddRange(new[] { "AHGhostCoopLogic", "AHGhostCoopLogicEditor" });
    }
}
