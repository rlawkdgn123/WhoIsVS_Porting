#pragma once

// YunoNetTransport/TcpClient(boost::asio)의 언리얼 이식판.
// - FSocket + FRunnable 스레드로 동일한 프레이밍(8B 헤더 + 바디)을 처리
// - 수신: "완성 패킷(헤더+바디)" 단위로 큐에 적재 → 게임 스레드에서 Pop 후 PacketDispatcher로 전달
// - 송신: PacketBuilder::Build 결과 바이트를 그대로 큐잉 → 넷 스레드에서 전송

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/Queue.h"

#include <atomic>
#include <vector>
#include <cstdint>

class FSocket;

namespace yuno::ue
{
    class FYunoTcpClient final : public FRunnable
    {
    public:
        FYunoTcpClient() = default;
        virtual ~FYunoTcpClient() override;

        FYunoTcpClient(const FYunoTcpClient&) = delete;
        FYunoTcpClient& operator=(const FYunoTcpClient&) = delete;

        // 접속 시작 (넷 스레드 기동). Host는 "127.0.0.1" 같은 IP/호스트명
        bool Start(const FString& Host, uint16 Port);
        void Shutdown();

        bool IsConnected() const { return bConnected.load(); }
        bool IsRunning() const { return bRunning.load(); }

        // 게임 스레드 → 넷 스레드 송신 큐
        void Send(std::vector<std::uint8_t>&& PacketBytes);

        // 넷 스레드 → 게임 스레드 수신 큐 (완성 패킷)
        bool PopIncoming(std::vector<std::uint8_t>& Out);

        // FRunnable
        virtual uint32 Run() override;
        virtual void Stop() override;

    private:
        bool ConnectBlocking();
        void CloseSocket();

        // RecvBuf에서 완성 패킷(8B 헤더 + 바디)을 뽑아 InQ로 밀어넣기
        bool ExtractFrames();

    private:
        FString ServerHost;
        uint16  ServerPort = 0;

        FSocket* Socket = nullptr;
        FRunnableThread* Thread = nullptr;

        std::atomic<bool> bRunning{ false };
        std::atomic<bool> bConnected{ false };

        TQueue<std::vector<std::uint8_t>, EQueueMode::Mpsc> OutQ; // 송신 대기
        TQueue<std::vector<std::uint8_t>, EQueueMode::Spsc> InQ;  // 수신 완성 패킷

        TArray<uint8> RecvBuf; // 수신 스트림 조립 버퍼
    };
}
