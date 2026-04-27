using UnrealBuildTool;

public class ProjectTDTarget : TargetRules
{
    public ProjectTDTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("ProjectTD");
    }
}
