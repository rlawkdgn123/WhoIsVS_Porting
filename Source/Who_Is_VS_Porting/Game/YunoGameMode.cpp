#include "YunoGameMode.h"

#include "Blueprint/UserWidget.h"
#include "UI/YunoDebugHudWidget.h"

void AYunoGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        if (UYunoDebugHudWidget* Hud = CreateWidget<UYunoDebugHudWidget>(PC, UYunoDebugHudWidget::StaticClass()))
        {
            Hud->AddToViewport();
        }
    }
}
