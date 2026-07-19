#pragma once

// YunoGame/ClientNetwork/YunoClientNetwork의 언리얼 이식판.
// GameInstance 수명에 맞춰 넷 스레드를 돌리고,
// 틱마다 수신 큐를 PacketDispatcher로 펌핑한다.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"

// 이식된 프로토콜 헤더들은 원본 pch.h의 std include에 의존하므로 먼저 채워준다
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

#include "PacketDispatcher.h" // yuno::net (이식된 프로토콜)
#include "NetPeer.h"
#include "Transport/YunoTcpClient.h" // unique_ptr 소멸자 때문에 완전 정의 필요

#include "YunoNetSubsystem.generated.h"

// UI(UMG/BP)가 구독하는 매치 이벤트들 — 원본의 GameManager SceneState 전환에 대응
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FYunoEnterOK, int32, SlotIdx, int32, PlayerCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FYunoReadyState, bool, bP1Ready, bool, bP2Ready);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYunoCountDown, int32, CountTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FYunoRoundStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FYunoTurnStart, int32, TurnNumber, int32, MyCardCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYunoDrawCandidates, const TArray<int64>&, RuntimeIds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYunoEndGame, int32, WinnerPID);

UCLASS()
class WHO_IS_VS_PORTING_API UYunoNetSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 환경변수 YUNO_SERVER_HOST/PORT (기본 127.0.0.1:9000)로 접속
    UFUNCTION(BlueprintCallable, Category = "YunoNet")
    bool ConnectToServer();

    UFUNCTION(BlueprintCallable, Category = "YunoNet")
    void DisconnectFromServer();

    UFUNCTION(BlueprintCallable, Category = "YunoNet")
    bool IsConnected() const;

    // M1 검증용: Ping / MatchEnter
    UFUNCTION(BlueprintCallable, Category = "YunoNet")
    void SendPing(int32 Nonce = 1000);

    UFUNCTION(BlueprintCallable, Category = "YunoNet")
    void SendMatchEnter();

    // 완성 패킷 바이트 송신 (게임 로직에서 PacketBuilder 결과를 그대로)
    void SendPacket(std::vector<std::uint8_t>&& PacketBytes);

    yuno::net::PacketDispatcher& Dispatcher() { return NetDispatcher; }

    // ── UI용 이벤트 (게임 스레드에서 브로드캐스트) ──
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoEnterOK OnEnterOK;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoReadyState OnReadyState;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoCountDown OnCountDown;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoRoundStart OnRoundStart;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoTurnStart OnTurnStart;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoDrawCandidates OnDrawCandidates;
    UPROPERTY(BlueprintAssignable, Category = "YunoNet|Events") FYunoEndGame OnEndGame;

    // ── UI용 액션 (봇 대신 유저 입력이 호출) ──
    // 자동 봇 켬/끔 (UI가 준비되면 끄고 아래 액션 사용)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "YunoNet")
    bool bAutoBot = true;

    UFUNCTION(BlueprintCallable, Category = "YunoNet|Actions")
    void SubmitWeapons(int32 WeaponId1, int32 WeaponId2);

    // 내 손패에서 고른 카드들(runtimeID)을 방향과 함께 제출. Dir: 1=Up 2=Down 3=Left 4=Right
    UFUNCTION(BlueprintCallable, Category = "YunoNet|Actions")
    void SubmitTurnCards(const TArray<int64>& RuntimeIds, uint8 Dir);

    UFUNCTION(BlueprintCallable, Category = "YunoNet|Actions")
    void SelectBonusCard(int64 RuntimeId);

    UFUNCTION(BlueprintCallable, Category = "YunoNet|State")
    int32 GetCurrentTurn() const { return CurrentTurn; }

    UFUNCTION(BlueprintCallable, Category = "YunoNet|State")
    int32 GetMyCardCount() const { return MyCardRuntimeIds.Num(); }

private:
    bool TickPump(float DeltaTime);
    void RegisterDefaultHandlers();

private:
    std::unique_ptr<yuno::ue::FYunoTcpClient> Client;

    yuno::net::PacketDispatcher NetDispatcher{ yuno::net::PacketDispatcher::EndpointRole::Client };
    yuno::net::NetPeer ServerPeer{}; // 클라는 서버 peer 하나 (sId=0)

    FTSTicker::FDelegateHandle TickHandle;
    bool bSentHello = false; // 연결 후 검증 Ping 1회 송신 여부

public:
    // ── 매치 상태 (원본 GameManager의 매치 흐름 필수 필드만 경량 이식) ──
    struct FRoundUnit
    {
        uint8 PID = 0, SlotID = 0, WeaponID = 0, HP = 0, Stamina = 0, SpawnTileId = 0;
    };

    int32 GetSlotIdx() const { return SlotIdx; }
    bool IsRoundStarted() const { return bRoundStarted; }

private:
    int32 SlotIdx = -1;          // EnterOK로 받는 내 슬롯 (원본 GameManager::SetSlotIdx)
    int32 MatchPlayerCount = 0;
    bool  bWeaponSubmitted = false;
    bool  bRoundStarted = false;
    FRoundUnit RoundUnits[4];    // RoundStart 수신 유닛 (원본 SetWeaponData)

    // ── 전투 턴 루프 (M3a: UI 전 단계, 자동 제출 봇으로 루프 검증) ──
    void SubmitTurnAuto();       // 내 카드 앞 4장을 dir=Up으로 제출 (원본 CardConfirm 흐름 대체)

    TArray<uint32> MyCardRuntimeIds; // StartCardList/StartTurn으로 받은 내 카드
    int32 CurrentTurn = 0;
    bool  bEndGame = false;
    int32 WinnerPID = 0;
};
