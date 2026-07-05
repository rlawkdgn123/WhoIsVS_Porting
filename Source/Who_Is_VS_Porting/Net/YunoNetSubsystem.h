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

private:
    bool TickPump(float DeltaTime);
    void RegisterDefaultHandlers();

private:
    std::unique_ptr<yuno::ue::FYunoTcpClient> Client;

    yuno::net::PacketDispatcher NetDispatcher{ yuno::net::PacketDispatcher::EndpointRole::Client };
    yuno::net::NetPeer ServerPeer{}; // 클라는 서버 peer 하나 (sId=0)

    FTSTicker::FDelegateHandle TickHandle;
    bool bSentHello = false; // 연결 후 검증 Ping 1회 송신 여부
};
