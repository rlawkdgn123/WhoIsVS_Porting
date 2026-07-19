#pragma once

// 포팅 기본 게임모드: 시작 시 디버그 HUD를 띄운다.
// (원본 GameApp::OnInit의 씬 스택 시작에 대응하는 자리)

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "YunoGameMode.generated.h"

UCLASS()
class WHO_IS_VS_PORTING_API AYunoGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
};
