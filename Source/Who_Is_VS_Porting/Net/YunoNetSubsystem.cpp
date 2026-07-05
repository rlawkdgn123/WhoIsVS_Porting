#include "YunoNetSubsystem.h"

#include "Transport/YunoTcpClient.h"

#include "PacketBuilder.h"
#include "PacketHeader.h"
#include "ByteIO.h"
#include "C2S_Ping.h"
#include "C2S_MatchEnter.h"
#include "S2C_Pong.h"
#include "S2C_EnterOK.h"

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

    NetDispatcher.RegisterRaw(PacketType::S2C_EnterOK,
        [](const NetPeer&, const PacketHeader&, const std::uint8_t* Body, std::uint32_t BodyLen)
        {
            ByteReader R(Body, BodyLen);
            const auto Ok = packets::S2C_EnterOK::Deserialize(R);
            UE_LOG(LogYunoNetSub, Log, TEXT("[YunoNet] <- S2C_EnterOK slot=%u players=%u"),
                Ok.slotIndex, Ok.playerCount);
        });
}

bool UYunoNetSubsystem::TickPump(float /*DeltaTime*/)
{
    if (Client)
    {
        // 연결 성립 최초 1회: 검증용 Ping (원본 GameApp의 '1'키 Ping과 동일 계약)
        if (Client->IsConnected() && !bSentHello)
        {
            bSentHello = true;
            SendPing(1000);
        }

        std::vector<std::uint8_t> Packet;
        while (Client->PopIncoming(Packet))
        {
            NetDispatcher.Dispatch(ServerPeer, Packet);
        }
    }
    return true; // 계속 틱
}
