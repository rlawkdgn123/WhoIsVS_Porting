#include "YunoDebugHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

#include "Net/YunoNetSubsystem.h"

void UYunoDebugHudWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 코드로 위젯 트리 구성 (Canvas + TextBlock)
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
    WidgetTree->RootWidget = Root;

    StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
    StatusText->SetText(FText::FromString(TEXT("YunoNet: init...")));
    StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));

    UCanvasPanelSlot* TextSlot = Root->AddChildToCanvas(StatusText);
    TextSlot->SetPosition(FVector2D(40.f, 40.f));
    TextSlot->SetAutoSize(true);

    if (UYunoNetSubsystem* Net = GetGameInstance()->GetSubsystem<UYunoNetSubsystem>())
    {
        Net->OnEndGame.AddDynamic(this, &UYunoDebugHudWidget::HandleEndGame);
    }
}

void UYunoDebugHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    UYunoNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UYunoNetSubsystem>() : nullptr;
    if (!Net || !StatusText)
    {
        return;
    }

    FString Line = FString::Printf(
        TEXT("[WhoIsVS UE Port]\n접속: %s   슬롯: %d\n라운드시작: %s   턴: %d   내 카드: %d"),
        Net->IsConnected() ? TEXT("O") : TEXT("X"),
        Net->GetSlotIdx(),
        Net->IsRoundStarted() ? TEXT("O") : TEXT("X"),
        Net->GetCurrentTurn(),
        Net->GetMyCardCount());

    if (!EndGameLine.IsEmpty())
    {
        Line += TEXT("\n") + EndGameLine;
    }

    StatusText->SetText(FText::FromString(Line));
}

void UYunoDebugHudWidget::HandleEndGame(int32 WinnerPID)
{
    EndGameLine = (WinnerPID == -1)
        ? TEXT("게임 종료: 무승부")
        : FString::Printf(TEXT("게임 종료: %dP 승리"), WinnerPID);
}
