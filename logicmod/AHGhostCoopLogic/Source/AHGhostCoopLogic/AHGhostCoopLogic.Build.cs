using UnrealBuildTool;

public class AHGhostCoopLogic : ModuleRules
{
    public AHGhostCoopLogic(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
    }
}
