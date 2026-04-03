// Copyright XujiaqiKumo. All Rights Reserved.

/*
 * MetahumanGizmoRuntime.cpp
 *
 * 中文：插件模块入口 — DEFINE_LOG_CATEGORY(LogMetahumanGizmoRuntime)、IMPLEMENT_MODULE。
 *       StartupModule / ShutdownModule 占位；功能类（如 UMetahumanFaceGizmoComponent）由模块加载后照常注册。
 * English: Module bootstrap and log category definition for MetahumanGizmoRuntime.
 */

#include "MetahumanGizmoRuntime.h"

DEFINE_LOG_CATEGORY(LogMetahumanGizmoRuntime);

#define LOCTEXT_NAMESPACE "FMetahumanGizmoRuntimeModule"

void FMetahumanGizmoRuntimeModule::StartupModule()
{
}

void FMetahumanGizmoRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMetahumanGizmoRuntimeModule, MetahumanGizmoRuntime)
