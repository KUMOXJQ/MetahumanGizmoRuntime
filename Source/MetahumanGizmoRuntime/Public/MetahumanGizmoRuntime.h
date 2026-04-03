// Copyright XujiaqiKumo. All Rights Reserved.

/*
 * MetahumanGizmoRuntime.h
 *
 * 中文：MetahumanGizmoRuntime 插件模块接口 — 声明日志类别 LogMetahumanGizmoRuntime 与 FMetahumanGizmoRuntimeModule。
 *       实现见 MetahumanGizmoRuntime.cpp（Startup/Shutdown 当前为空，仅注册模块与日志）。
 * English: IModuleInterface for the plugin; DECLARE_LOG_CATEGORY_EXTERN for runtime gizmo diagnostics.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetahumanGizmoRuntime, Log, All);

class FMetahumanGizmoRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
