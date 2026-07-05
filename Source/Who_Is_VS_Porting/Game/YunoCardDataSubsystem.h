#pragma once

// 카드 데이터(CSV) 로딩 소유자.
// 원본은 GameManager 생성 시 CardManager/CardRangeManager를 로드했음
// (YunoGame/GameManager/GameManager.cpp 참조) — UE에선 GameInstance 수명으로 대체.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Data/CardManager.h"
#include "Data/CardRangeManager.h"

#include "YunoCardDataSubsystem.generated.h"

UCLASS()
class WHO_IS_VS_PORTING_API UYunoCardDataSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "YunoCard")
    bool IsLoaded() const { return bLoaded; }

    UFUNCTION(BlueprintCallable, Category = "YunoCard")
    int32 GetCardCount() const;

    CardManager& Cards() { return CardMgr; }
    CardRangeManager& Ranges() { return RangeMgr; }

private:
    CardManager CardMgr;
    CardRangeManager RangeMgr;
    bool bLoaded = false;
};
