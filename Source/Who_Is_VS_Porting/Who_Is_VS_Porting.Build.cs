// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Who_Is_VS_Porting : ModuleRules
{
	public Who_Is_VS_Porting(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// TCP 통신용 (YunoServer 접속) + UI(UMG)
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "UMG", "Slate", "SlateCore" });

		// YunoNetProtocol 이식 소스 (순수 C++, bare include 스타일 유지)
		PrivateIncludePaths.AddRange(new string[] {
			ModuleDirectory, // 모듈 루트 상대 include (Net/..., UI/..., Game/...)
			Path.Combine(ModuleDirectory, "Net/Protocol"),
			Path.Combine(ModuleDirectory, "Net/Protocol/C2SPackets"),
			Path.Combine(ModuleDirectory, "Net/Protocol/S2CPackets"),
			Path.Combine(ModuleDirectory, "Net/Protocol/ErrorPackets"),
		});

		// 이식된 std C++ 코드 호환 (ByteReader가 std::stdexcept 사용)
		bEnableExceptions = true;

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
	}
}
