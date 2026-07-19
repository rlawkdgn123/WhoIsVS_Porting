#include "YunoNetSubsystem.h"

#include "Transport/YunoTcpClient.h"

#include "PacketBuilder.h"
#include "PacketHeader.h"
#include "ByteIO.h"
#include "C2S_Ping.h"
#include "C2S_MatchEnter.h"
#include "C2S_ReadySet.h"
#include "C2S_SubmitWeapon.h"
#include "C2S_BattlePackets.h"
#include "C2S_CardPackets.h"
#include "S2C_Pong.h"
#include "S2C_EnterOK.h"
#include "S2C_ReadyState.h"
#include "S2C_CountDown.h"
#include "S2C_RoundStart.h"
#include "S2C_CardPackets.h"
#include "S2C_BattlePackets.h"
#include "S2C_EndGame.h"
#include "S2C_Error.h"

DEFINE_LOG_CATEGORY_STATIC(LogYunoNetSub, Log, All);

void UYunoNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Client = std::make_unique<yuno::ue::FYunoTcpClient>();
    RegisterDefaultHandlers();

    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UYunoNetSubsystem::TickPump));

    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] subsystem initialized"));

    // 원본 GameApp::OnInit과 동일하게 시작 시 바로 접속 시도
    ConnectToServer();
}

void UYunoNetSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);

    if (Client)
    {
        Client->Shutdown();
        Client.reset();
    }

    Super::Deinitialize();
}

bool UYunoNetSubsystem::ConnectToServer()
{
    // 원본 utilityClass.h와 동일한 환경변수 계약 (기본 127.0.0.1:9000)
    FString Host = FPlatformMisc::GetEnvironmentVariable(TEXT("YUNO_SERVER_HOST"));
    if (Host.IsEmpty())
    {
        Host = TEXT("127.0.0.1");
        UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] YUNO_SERVER_HOST not set. fallback=127.0.0.1"));
    }

    int32 Port = 9000;
    const FString PortStr = FPlatformMisc::GetEnvironmentVariable(TEXT("YUNO_SERVER_PORT"));
    if (!PortStr.IsEmpty())
    {
        Port = FCString::Atoi(*PortStr);
        if (Port <= 0 || Port > 65535) { Port = 9000; }
    }

    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] connect target=%s:%d"), *Host, Port);
    return Client && Client->Start(Host, static_cast<uint16>(Port));
}

void UYunoNetSubsystem::DisconnectFromServer()
{
    if (Client)
    {
        Client->Shutdown();
    }
}

bool UYunoNetSubsystem::IsConnected() const
{
    return Client && Client->IsConnected();
}

void UYunoNetSubsystem::SendPacket(std::vector<std::uint8_t>&& PacketBytes)
{
    if (Client)
    {
        Client->Send(MoveTemp(PacketBytes));
    }
}

void UYunoNetSubsystem::SendPing(int32 Nonce)
{
    using namespace yuno::net;

    packets::C2S_Ping Ping{};
    Ping.nonce = static_cast<std::uint32_t>(Nonce);

    auto Bytes = PacketBuilder::Build(PacketType::C2S_Ping,
        [&](ByteWriter& W) { Ping.Serialize(W); });

    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_Ping nonce=%d"), Nonce);
    SendPacket(MoveTemp(Bytes));
}

void UYunoNetSubsystem::SendMatchEnter()
{
    using namespace yuno::net;

    // 원본 계약: YUNO_USER_ID 환경변수로 유저 구분 (기본 1)
    int32 UserId = 1;
    const FString IdStr = FPlatformMisc::GetEnvironmentVariable(TEXT("YUNO_USER_ID"));
    if (!IdStr.IsEmpty()) { UserId = FCString::Atoi(*IdStr); }

    packets::C2S_MatchEnter Enter{};
    Enter.userId = static_cast<std::uint32_t>(UserId);

    auto Bytes = PacketBuilder::Build(PacketType::C2S_MatchEnter,
        [&](ByteWriter& W) { Enter.Serialize(W); });

    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_MatchEnter userId=%d"), UserId);
    SendPacket(MoveTemp(Bytes));
}

// 원본 YunoClientNetwork::RegisterMatchPacketHandler의 매치 진입 흐름 이식:
// MatchEnter → EnterOK(슬롯) → ReadySet → ReadyState(양측 레디) → SubmitWeapon
// → 서버가 CountDown + RoundStart 브로드캐스트 (RoundController::TryStartRound)
void UYunoNetSubsystem::RegisterDefaultHandlers()
{
    using namespace yuno::net;

    NetDispatcher.RegisterRaw(PacketType::S2C_Pong,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pong = packets::S2C_Pong::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_Pong nonce=%u"), Pong.nonce);
        });

    // EnterOK: 슬롯 저장 후 곧바로 레디 (원본은 UI 레디버튼 → 여기선 자동)
    NetDispatcher.RegisterRaw(PacketType::S2C_EnterOK,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Ok = packets::S2C_EnterOK::Deserialize(R);
            SlotIdx = Ok.slotIndex;
            MatchPlayerCount = Ok.playerCount;
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_EnterOK slot=%u players=%u"),
                Ok.slotIndex, Ok.playerCount);
            OnEnterOK.Broadcast(SlotIdx, MatchPlayerCount);

            packets::C2S_ReadySet Ready{};
            Ready.readyState = 1;
            auto Bytes = PacketBuilder::Build(PacketType::C2S_ReadySet,
                [&](ByteWriter& W) { Ready.Serialize(W); });
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_ReadySet ready=1"));
            SendPacket(MoveTemp(Bytes));
        });

    // ReadyState: 양측 레디면 무기 제출 (원본과 동일 흐름, 무기는 테스트 픽스처)
    NetDispatcher.RegisterRaw(PacketType::S2C_ReadyState,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto St = packets::S2C_ReadyState::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_ReadyState p1=%u p2=%u"),
                St.p1_isReady, St.p2_isReady);
            OnReadyState.Broadcast(St.p1_isReady != 0, St.p2_isReady != 0);

            if (!(St.p1_isReady && St.p2_isReady) || bWeaponSubmitted || SlotIdx < 0)
            {
                return;
            }

            // 봇 모드: 슬롯별 고정 픽 (1P: 블래스터+차크람 / 2P: 브리처+사이드)
            // UI 모드: OnReadyState를 받은 무기선택 화면이 SubmitWeapons() 호출
            if (bAutoBot)
            {
                SubmitWeapons((SlotIdx == 1) ? 1 : 3, (SlotIdx == 1) ? 2 : 4);
            }
        });

    NetDispatcher.RegisterRaw(PacketType::S2C_CountDown,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Cd = packets::S2C_CountDown::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_CountDown t=%u units=(%u,%u | %u,%u)"),
                Cd.countTime, Cd.slot1_UnitId1, Cd.slot1_UnitId2, Cd.slot2_UnitId1, Cd.slot2_UnitId2);
            OnCountDown.Broadcast(Cd.countTime);
        });

    // RoundStart: 유닛 4기 초기 데이터 저장 (원본 SetWeaponData 대응)
    NetDispatcher.RegisterRaw(PacketType::S2C_RoundStart,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Rs = packets::S2C_RoundStart::Deserialize(R);
            bRoundStarted = true;

            for (int32 i = 0; i < 4; ++i)
            {
                const auto& U = Rs.units[i];
                RoundUnits[i] = { U.PID, U.slotID, U.WeaponID, U.hp, U.stamina, U.SpawnTileId };
                UE_LOG(LogYunoNetSub, Log,
                    TEXT("[YunoNet] <- S2C_RoundStart unit pid=%u slot=%u weapon=%u hp=%u sta=%u tile=%u"),
                    U.PID, U.slotID, U.WeaponID, U.hp, U.stamina, U.SpawnTileId);
            }
            OnRoundStart.Broadcast();
        });

    NetDispatcher.RegisterRaw(PacketType::S2C_Error,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            UE_LOG(LogYunoNetSub, Warning, TEXT("[YunoNet] <- S2C_Error bodyLen=%u"), BodyLen);
        });

    // ── 전투 턴 루프 (원본 YunoClientNetwork 300~600행 흐름) ──

    // StartCardList: 라운드 시작 카드 배정 — 내 것만 보관
    NetDispatcher.RegisterRaw(PacketType::S2C_StartCardList,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_StartCardList::Deserialize(R);

            MyCardRuntimeIds.Reset();
            for (const auto& C : Pkt.cards)
            {
                if (C.PID == static_cast<uint8_t>(SlotIdx))
                {
                    MyCardRuntimeIds.Add(C.runtimeID);
                }
            }
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_StartCardList total=%d mine=%d"),
                static_cast<int32>(Pkt.cards.size()), MyCardRuntimeIds.Num());
        });

    // StartTurn: 턴 시작 + 드로우 2장 → 자동 제출 (M3a: UI 전 봇 검증)
    NetDispatcher.RegisterRaw(PacketType::S2C_StartTurn,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_StartTurn::Deserialize(R);
            CurrentTurn = Pkt.turnNumber;

            for (const auto& C : Pkt.addedCards)
            {
                if (C.PID == static_cast<uint8_t>(SlotIdx) && C.runtimeID != 0)
                {
                    MyCardRuntimeIds.Add(C.runtimeID);
                }
            }
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_StartTurn turn=%d (myCards=%d)"),
                CurrentTurn, MyCardRuntimeIds.Num());
            OnTurnStart.Broadcast(CurrentTurn, MyCardRuntimeIds.Num());

            if (bAutoBot)
            {
                SubmitTurnAuto();
            }
        });

    // BattleResult: 카드 1장 해석 결과 (턴당 여러 번 수신)
    NetDispatcher.RegisterRaw(PacketType::S2C_BattleResult,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_BattleResult::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log,
                TEXT("[YunoNet] <- S2C_BattleResult card=%u owner=%u unit=%u steps=%d"),
                Pkt.runtimeCardId, Pkt.ownerSlot, Pkt.unitLocalIndex,
                static_cast<int32>(Pkt.order.size()));
        });

    // ObstacleResult = 턴 연출 마지막 패킷. 원본은 PlayGridSystem이 연출 소진 후
    // C2S_RoundStartReadyOK를 보냄(PlayGridSystem.cpp:518) — 봇은 즉시 전송
    // (라운드 진행 중엔 서버가 무시하므로 안전)
    NetDispatcher.RegisterRaw(PacketType::S2C_ObstacleResult,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_ObstacleResult::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_ObstacleResult obstacles=%d"),
                static_cast<int32>(Pkt.obstacles.size()));

            packets::C2S_RoundStartReadyOK Ok{};
            auto Bytes = PacketBuilder::Build(PacketType::C2S_RoundStartReadyOK,
                [&](ByteWriter& W) { Ok.Serialize(W); });
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_RoundStartReadyOK (연출완료 ACK)"));
            SendPacket(MoveTemp(Bytes));
        });

    // DrawCandidates: 보너스 카드 후보 3장 — 봇은 첫 장 자동 선택
    // (양쪽 모두 선택해야 서버가 다음 턴 시작: RoundController::OnPlayerSelectedCard)
    NetDispatcher.RegisterRaw(PacketType::S2C_DrawCandidates,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_DrawCandidates::Deserialize(R);

            TArray<int64> MyCandidates;
            for (const auto& C : Pkt.cards)
            {
                if (C.PID == static_cast<uint8_t>(SlotIdx) && C.runtimeID != 0)
                {
                    MyCandidates.Add(static_cast<int64>(C.runtimeID));
                }
            }
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_DrawCandidates n=%d mine=%d"),
                static_cast<int32>(Pkt.cards.size()), MyCandidates.Num());
            OnDrawCandidates.Broadcast(MyCandidates);

            if (bAutoBot && MyCandidates.Num() > 0)
            {
                SelectBonusCard(MyCandidates[0]);
            }
        });

    // EndGame: 승자 판정 (원본 로직 동일)
    NetDispatcher.RegisterRaw(PacketType::S2C_EndGame,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Pkt = packets::S2C_EndGame::Deserialize(R);

            const uint8 P1 = Pkt.results[0].winCount;
            const uint8 P2 = Pkt.results[1].winCount;
            WinnerPID = (P1 > P2) ? 1 : (P1 < P2) ? 2 : -1;
            bEndGame = true;

            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_EndGame p1Wins=%u p2Wins=%u winner=%d"),
                P1, P2, WinnerPID);
            OnEndGame.Broadcast(WinnerPID);
        });

    NetDispatcher.RegisterRaw(PacketType::S2C_EndGame_Disconnect,
        [this](const NetPeer&, const PacketHeader&, const std::uint8_t*, std::uint32_t)
        {
            bEndGame = true;
            UE_LOG(LogYunoNetSub, Warning, TEXT("[YunoNet] <- S2C_EndGame_Disconnect (상대 이탈)"));
        });
}

// ── UI/봇 공용 액션 구현 ──

void UYunoNetSubsystem::SubmitWeapons(int32 WeaponId1, int32 WeaponId2)
{
    using namespace yuno::net;

    if (bWeaponSubmitted) { return; }
    bWeaponSubmitted = true;

    packets::C2S_SubmitWeapon Req{};
    Req.WeaponId1 = static_cast<std::uint8_t>(WeaponId1);
    Req.WeaponId2 = static_cast<std::uint8_t>(WeaponId2);

    auto Bytes = PacketBuilder::Build(PacketType::C2S_SubmitWeapon,
        [&](ByteWriter& W) { Req.Serialize(W); });
    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_SubmitWeapon %d,%d"), WeaponId1, WeaponId2);
    SendPacket(MoveTemp(Bytes));
}

void UYunoNetSubsystem::SubmitTurnCards(const TArray<int64>& RuntimeIds, uint8 Dir)
{
    using namespace yuno::net;

    packets::C2S_ReadyTurn Req{};
    for (const int64 Id : RuntimeIds)
    {
        CardPlayCommand Cmd{};
        Cmd.runtimeID = static_cast<uint32_t>(Id);
        Cmd.dir = static_cast<Direction>(Dir);
        Req.commands.push_back(Cmd);
    }

    auto Bytes = PacketBuilder::Build(PacketType::C2S_ReadyTurn,
        [&](ByteWriter& W) { Req.Serialize(W); });
    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_ReadyTurn commands=%d"), RuntimeIds.Num());
    SendPacket(MoveTemp(Bytes));
}

void UYunoNetSubsystem::SelectBonusCard(int64 RuntimeId)
{
    using namespace yuno::net;

    packets::C2S_SelectCard Sel{};
    Sel.runtimeID = static_cast<uint32_t>(RuntimeId);

    auto Bytes = PacketBuilder::Build(PacketType::C2S_SelectCard,
        [&](ByteWriter& W) { Sel.Serialize(W); });
    UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_SelectCard runtimeID=%lld"), RuntimeId);
    SendPacket(MoveTemp(Bytes));
}

// 내 카드 앞 4장을 dir=Up으로 제출 — 봇 모드 전용 (bAutoBot)
void UYunoNetSubsystem::SubmitTurnAuto()
{
    TArray<int64> Picks;
    const int32 SubmitCount = FMath::Min(4, MyCardRuntimeIds.Num());
    for (int32 i = 0; i < SubmitCount; ++i)
    {
        Picks.Add(static_cast<int64>(MyCardRuntimeIds[i]));
    }
    SubmitTurnCards(Picks, static_cast<uint8>(Direction::Up));
}

bool UYunoNetSubsystem::TickPump(float /*DeltaTime*/)
{
    if (Client)
    {
        // 연결 성립 최초 1회: 검증 Ping + 매치 진입 (원본 Title→RequestEnter 흐름의 자동화)
        if (Client->IsConnected() && !bSentHello)
        {
            bSentHello = true;
            SendPing(1000);
            SendMatchEnter();
        }

        std::vector<std::uint8_t> Packet;
        while (Client->PopIncoming(Packet))
        {
            NetDispatcher.Dispatch(ServerPeer, Packet);
        }
    }
    return true; // 계속 틱
}
