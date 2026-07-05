#include "YunoTcpClient.h"

#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"

#include "PacketHeader.h" // yuno::net (이식된 프로토콜)

DEFINE_LOG_CATEGORY_STATIC(LogYunoNet, Log, All);

namespace yuno::ue
{
    FYunoTcpClient::~FYunoTcpClient()
    {
        Shutdown();
    }

    bool FYunoTcpClient::Start(const FString& Host, uint16 Port)
    {
        if (bRunning.load())
        {
            UE_LOG(LogYunoNet, Warning, TEXT("[YunoTcp] already running"));
            return false;
        }

        ServerHost = Host;
        ServerPort = Port;

        bRunning.store(true);
        Thread = FRunnableThread::Create(this, TEXT("YunoTcpClient"), 0, TPri_Normal);
        if (!Thread)
        {
            bRunning.store(false);
            UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] thread create failed"));
            return false;
        }
        return true;
    }

    void FYunoTcpClient::Shutdown()
    {
        Stop();

        if (Thread)
        {
            Thread->WaitForCompletion();
            delete Thread;
            Thread = nullptr;
        }
        CloseSocket();
    }

    void FYunoTcpClient::Stop()
    {
        bRunning.store(false);
    }

    void FYunoTcpClient::Send(std::vector<std::uint8_t>&& PacketBytes)
    {
        if (!bRunning.load())
        {
            UE_LOG(LogYunoNet, Warning, TEXT("[YunoTcp] send while not running (dropped)"));
            return;
        }
        OutQ.Enqueue(MoveTemp(PacketBytes));
    }

    bool FYunoTcpClient::PopIncoming(std::vector<std::uint8_t>& Out)
    {
        return InQ.Dequeue(Out);
    }

    bool FYunoTcpClient::ConnectBlocking()
    {
        ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!SS)
        {
            UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] no socket subsystem"));
            return false;
        }

        // IP 리터럴 우선, 실패 시 DNS 조회
        TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
        bool bValid = false;
        Addr->SetIp(*ServerHost, bValid);
        if (!bValid)
        {
            FAddressInfoResult GAIResult = SS->GetAddressInfo(*ServerHost, nullptr,
                EAddressInfoFlags::Default, NAME_None, SOCKTYPE_Streaming);
            if (GAIResult.ReturnCode == SE_NO_ERROR && GAIResult.Results.Num() > 0)
            {
                Addr = GAIResult.Results[0].Address;
                bValid = true;
            }
        }
        if (!bValid)
        {
            UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] resolve failed: %s"), *ServerHost);
            return false;
        }
        Addr->SetPort(ServerPort);

        Socket = FTcpSocketBuilder(TEXT("YunoTcpClientSocket"))
            .AsBlocking()
            .WithReceiveBufferSize(256 * 1024)
            .WithSendBufferSize(256 * 1024);

        if (!Socket)
        {
            UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] socket create failed"));
            return false;
        }

        UE_LOG(LogYunoNet, Log, TEXT("[YunoTcp] connecting %s:%d ..."), *ServerHost, ServerPort);
        if (!Socket->Connect(*Addr))
        {
            UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] connect failed: %s:%d"), *ServerHost, ServerPort);
            CloseSocket();
            return false;
        }

        Socket->SetNonBlocking(true); // 이후 폴링 루프
        bConnected.store(true);
        UE_LOG(LogYunoNet, Log, TEXT("[YunoTcp] connected %s:%d"), *ServerHost, ServerPort);
        return true;
    }

    void FYunoTcpClient::CloseSocket()
    {
        bConnected.store(false);
        if (Socket)
        {
            Socket->Close();
            if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
            {
                SS->DestroySocket(Socket);
            }
            Socket = nullptr;
        }
    }

    bool FYunoTcpClient::ExtractFrames()
    {
        using namespace yuno::net;

        while (RecvBuf.Num() >= static_cast<int32>(yunoPacketHeaderSize))
        {
            const PacketHeader H = UnPackHeaderLE(RecvBuf.GetData());
            if (!IsHeaderBodyLengthSane(H.bodyLength))
            {
                UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] insane bodyLen=%u — disconnect"), H.bodyLength);
                return false;
            }

            const int32 Total = static_cast<int32>(yunoPacketHeaderSize + H.bodyLength);
            if (RecvBuf.Num() < Total)
            {
                break; // 바디 마저 수신 대기
            }

            std::vector<std::uint8_t> Packet(RecvBuf.GetData(), RecvBuf.GetData() + Total);
            InQ.Enqueue(MoveTemp(Packet));
            RecvBuf.RemoveAt(0, Total, EAllowShrinking::No);
        }
        return true;
    }

    uint32 FYunoTcpClient::Run()
    {
        if (!ConnectBlocking())
        {
            bRunning.store(false);
            return 1;
        }

        uint8 TempBuf[64 * 1024];

        while (bRunning.load())
        {
            bool bDidWork = false;

            // 1) 송신 큐 비우기
            std::vector<std::uint8_t> OutPkt;
            while (OutQ.Dequeue(OutPkt))
            {
                int32 TotalSent = 0;
                const int32 ToSend = static_cast<int32>(OutPkt.size());
                while (TotalSent < ToSend && bRunning.load())
                {
                    int32 Sent = 0;
                    if (!Socket->Send(OutPkt.data() + TotalSent, ToSend - TotalSent, Sent))
                    {
                        UE_LOG(LogYunoNet, Error, TEXT("[YunoTcp] send failed — disconnect"));
                        CloseSocket();
                        bRunning.store(false);
                        return 1;
                    }
                    if (Sent > 0) { TotalSent += Sent; }
                    else { FPlatformProcess::SleepNoStats(0.001f); } // 송신버퍼 가득참
                }
                bDidWork = true;
            }

            // 2) 수신
            uint32 Pending = 0;
            while (Socket->HasPendingData(Pending) && Pending > 0)
            {
                int32 Read = 0;
                const int32 ToRead = FMath::Min(static_cast<int32>(Pending), static_cast<int32>(sizeof(TempBuf)));
                if (!Socket->Recv(TempBuf, ToRead, Read) || Read <= 0)
                {
                    break;
                }
                RecvBuf.Append(TempBuf, Read);
                bDidWork = true;
            }

            if (!ExtractFrames())
            {
                break; // 프로토콜 위반 → 종료
            }

            // 3) 연결 상태 체크
            if (Socket->GetConnectionState() == SCS_ConnectionError)
            {
                UE_LOG(LogYunoNet, Warning, TEXT("[YunoTcp] connection error — disconnect"));
                break;
            }

            if (!bDidWork)
            {
                Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(10));
            }
        }

        CloseSocket();
        bRunning.store(false);
        UE_LOG(LogYunoNet, Log, TEXT("[YunoTcp] net thread exit"));
        return 0;
    }
}
