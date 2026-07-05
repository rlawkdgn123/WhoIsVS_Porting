#include "YunoCardDataSubsystem.h"

#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogYunoCardSub, Log, All);

void UYunoCardDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 원본 경로 계약(../Assets/CardData/...)을 UE Content 기준으로 치환
    const FString Dir = FPaths::ProjectContentDir() / TEXT("CardData");

    const FString CardCsv  = Dir / TEXT("Carddata(Cheat).csv");
    const FString RangeCsv = Dir / TEXT("CardRange.csv");

    const bool bCards  = CardMgr.LoadFromCSV(TCHAR_TO_UTF8(*CardCsv));
    const bool bRanges = RangeMgr.LoadFromCSV(TCHAR_TO_UTF8(*RangeCsv));

    bLoaded = bCards && bRanges;

    if (!bLoaded)
    {
        UE_LOG(LogYunoCardSub, Error, TEXT("[YunoCard] CSV load failed (cards=%d ranges=%d) dir=%s"),
            bCards ? 1 : 0, bRanges ? 1 : 0, *Dir);
    }
}

int32 UYunoCardDataSubsystem::GetCardCount() const
{
    return static_cast<int32>(CardMgr.GetCardCount());
}
