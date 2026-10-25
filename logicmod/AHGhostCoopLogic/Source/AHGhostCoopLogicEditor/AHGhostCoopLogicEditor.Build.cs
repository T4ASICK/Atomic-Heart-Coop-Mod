using UnrealBuildTool;

public class AHGhostCoopLogicEditor : ModuleRules
{
    public AHGhostCoopLogicEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AHGhostCoopLogic",
            "AssetRegistry",
            "BlueprintGraph",
            "Kismet",
            "KismetCompiler",
            "UMG",
            "UMGEditor",
            "Slate",
            "SlateCore",
            "RenderCore",
            "SlateRHIRenderer",
            "UnrealEd"
        });
    }
}
