// Copyright XujiaqiKumo. All Rights Reserved.

using UnrealBuildTool;

public class MetahumanGizmoRuntime : ModuleRules
{
	public MetahumanGizmoRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bAllowUETypesInNamespaces = false;

		// Epic 预编译引擎下 MetaHumanCoreTechLib 通常只有 Editor 的预编译清单；Game 目标不链该模块，由 cpp 中
		// WITH_METAHUMAN_GIZMO_RUNTIME_EVAL 提供 Evaluate/EvaluateGizmos 完整路径（Editor），否则为占位实现。
		bool bWithGizmoEval = (Target.Type == TargetType.Editor);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		if (bWithGizmoEval)
		{
			PublicDependencyModuleNames.Add("MetaHumanCoreTechLib");
			PrivateDefinitions.Add("WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=0");
		}

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MetaHumanCharacter",
			"MetaHumanSDKRuntime",
			"Projects",
			"RigLogicModule",
		});
	}
}
