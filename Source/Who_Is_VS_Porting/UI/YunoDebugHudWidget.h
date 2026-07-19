#pragma once

// 매치 상태를 화면에 뿌리는 임시 디버그 HUD (에디터 UMG 에셋 없이 순수 C++).
// M3b 본 UI(UMG 블루프린트)가 만들어지면 이 위젯은 참고용/개발용으로 남긴다.
// UI팀은 UYunoNetSubsystem의 On* 델리게이트 + Submit*/Select* 액션을 쓰면 된다.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "YunoDebugHudWidget.generated.h"

class UTextBlock;

UCLASS()
class WHO_IS_VS_PORTING_API UYunoDebugHudWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UPROPERTY() TObjectPtr<UTextBlock> StatusText;

    UFUNCTION() void HandleEndGame(int32 WinnerPID);

    FString EndGameLine;
};
