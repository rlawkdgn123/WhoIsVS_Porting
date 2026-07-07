#include "YunoNetSubsystem.h"

#include "Transport/YunoTcpClient.h"

#include "PacketBuilder.h"
#include "PacketHeader.h"
#include "ByteIO.h"
#include "C2S_Ping.h"
#include "C2S_MatchEnter.h"
#include "C2S_ReadySet.h"
#include "C2S_SubmitWeapon.h"
#include "S2C_Pong.h"
#include "S2C_EnterOK.h"
#include "S2C_ReadyState.h"
#include "S2C_CountDown.h"
#include "S2C_RoundStart.h"
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

            if (!(St.p1_isReady && St.p2_isReady) || bWeaponSubmitted || SlotIdx < 0)
            {
                return;
            }
            bWeaponSubmitted = true;

            // TODO(M3): 무기선택 UI 연결 전까지 슬롯별 고정 픽 (1:블래스터 2:차크람 3:브리처 4:사이드)
            // slotIndex는 1-base (검증 로그 기준 1P=1, 2P=2)
            packets::C2S_SubmitWeapon Req{};
            Req.WeaponId1 = (SlotIdx == 1) ? 1 : 3;
            Req.WeaponId2 = (SlotIdx == 1) ? 2 : 4;

            auto Bytes = PacketBuilder::Build(PacketType::C2S_SubmitWeapon,
                [&](ByteWriter& W) { Req.Serialize(W); });
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] -> C2S_SubmitWeapon %u,%u"),
                Req.WeaponId1, Req.WeaponId2);
            SendPacket(MoveTemp(Bytes));
        });

    NetDispatcher.RegisterRaw(PacketType::S2C_CountDown,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Cd = packets::S2C_CountDown::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_CountDown t=%u units=(%u,%u | %u,%u)"),
                Cd.countTime, Cd.slot1_UnitId1, Cd.slot1_UnitId2, Cd.slot2_UnitId1, Cd.slot2_UnitId2);
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
        });

    NetDispatcher.RegisterRaw(PacketType::S2C_Error,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            UE_LOG(LogYunoNetSub, Warning, TEXT("[YunoNet] <- S2C_Error bodyLen=%u"), BodyLen);
        });
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
