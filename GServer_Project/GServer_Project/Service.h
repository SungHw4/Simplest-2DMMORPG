#pragma once
#include "stdafx.h"
#include "Packet.h"
#include "ObjectQueue.h"
#include "InnerPacket.h"
#include "game_protocol_generated.h"

#include <functional>
#include <unordered_map>
#include <deque>
#include <vector>
#include <thread>

// -----------------------------------------------------------------------
// PacketProtocolID<MessageType>
//   각 FlatBuffers 메시지 타입에 대응하는 EPacketProtocol ID를
//   컴파일 타임에 매핑하는 traits 구조체.
//
//   NativeTableType이 생성되지 않는 프로젝트(object API 미사용)에서
//   RegisterHandler 가 프로토콜 ID를 자동 추출할 때 사용한다.
//
//   새 패킷 타입을 추가하면 이 곳에도 특수화를 추가한다.
// -----------------------------------------------------------------------
template<typename T>
struct PacketProtocolID
{
    // 특수화되지 않은 타입은 컴파일 에러를 내서 누락을 방지한다.
    static_assert(sizeof(T) == 0,
        "[PacketProtocolID] 이 메시지 타입의 특수화가 없습니다. "
        "Service.h 의 PacketProtocolID 에 특수화를 추가하세요.");
    static constexpr int value = 0;
};

// CS 패킷
template<> struct PacketProtocolID<GameProtocol::CSLogin>
    { static constexpr int value = 101; };
template<> struct PacketProtocolID<GameProtocol::CSPlayerMoveRequest>
    { static constexpr int value = 201; };
template<> struct PacketProtocolID<GameProtocol::CSPlayerAttackRequest>
    { static constexpr int value = 205; };
template<> struct PacketProtocolID<GameProtocol::CSPlayerChattingRequest>
    { static constexpr int value = 301; };
template<> struct PacketProtocolID<GameProtocol::CSRandomTeleportRequest>
    { static constexpr int value = 401; };

// SC 패킷 (서버 → 클라이언트, 필요 시 사용)
template<> struct PacketProtocolID<GameProtocol::SCPlayerMoveResponse>
    { static constexpr int value = 202; };
template<> struct PacketProtocolID<GameProtocol::SCPlayerAttackResponse>
    { static constexpr int value = 206; };
template<> struct PacketProtocolID<GameProtocol::SCPlayerChattingResponse>
    { static constexpr int value = 302; };
template<> struct PacketProtocolID<GameProtocol::SCIntegrationErrorNotification>
    { static constexpr int value = 500; };

// -----------------------------------------------------------------------
// Service (FSCore의 Service 패턴 적용)
//
// 역할:
//   - 네트워크 수신 패킷(Packet)을 내부 큐에 쌓고 전용 스레드에서 디스패치
//   - InnerPacket(서비스 간 내부 메시지)도 별도 큐로 처리
//   - RegisterHandler<Derived, MsgType>() 템플릿으로 핸들러 등록
//
// 사용 방법:
//   class GameService : public Service { ... };
//   GameService::GameService() {
//       RegisterHandler<GameService, GameProtocol::CSPlayerMoveRequest>
//           (&GameService::Handle_Move);
//   }
// -----------------------------------------------------------------------
class Service
{
private:
    // -- 패킷 큐 (네트워크 수신 패킷) --
    ObjectQueue<std::shared_ptr<Packet>>                       mPacketQueue;
    std::unordered_map<int, std::function<bool(const Packet&)>> mPacketHandler;
    std::deque<std::shared_ptr<Packet>>                        mPacketWorkList;

    // -- InnerPacket 큐 (서비스 간 내부 통신) --
    ObjectQueue<InnerPacket::SharedPtr>                              mInnerPacketQueue;
    std::unordered_map<int, std::function<bool(InnerPacket::SharedPtr)>> mInnerPacketHandler;
    std::deque<InnerPacket::SharedPtr>                               mInnerPacketWorkList;

    bool        mShouldExit = false;
    std::thread mThread;

public:
    Service() = default;
    virtual ~Service() { Exit(); }

    // 스레드 시작
    void StartThread()
    {
        mThread = std::thread([this]() { Run(); });
    }

    void Exit()
    {
        mShouldExit = true;
        if (mThread.joinable())
            mThread.join();
    }

    // 네트워크 수신 패킷 Push (NetworkEventSync::OnReceive 에서 호출)
    void Push(std::shared_ptr<Packet> pPacket)
    {
        if (pPacket != nullptr)
            mPacketQueue.Push(pPacket);
    }

    // 서비스 간 내부 패킷 Push
    void InnerPush(InnerPacket::SharedPtr pInner)
    {
        if (pInner != nullptr)
            mInnerPacketQueue.Push(pInner);
    }

    size_t GetPacketQueueCount()  { return mPacketQueue.Count(); }

    virtual void Run()
    {
        while (!mShouldExit)
        {
            _InnerPacketProcess();
            _PacketProcess();
        }
    }

protected:
    // -----------------------------------------------------------------------
    // RegisterHandler<Derived, MessageType>(멤버함수포인터)
    //   PacketProtocolID<MessageType>::value 로 프로토콜 ID 를 컴파일 타임에 추출.
    //   NativeTableType 없이도 동작하는 방식.
    //
    //   핸들러 시그니처: bool Derived::Handle_Xxx(int hostID, const MessageType& msg)
    // -----------------------------------------------------------------------
    template <typename DerivedType, typename MessageType,
        typename = typename std::enable_if<std::is_base_of<Service, DerivedType>::value>::type>
    void RegisterHandler(bool (DerivedType::* handler)(int, const MessageType&))
    {
        DerivedType* derived = static_cast<DerivedType*>(this);

        // NativeTableType 대신 PacketProtocolID traits 로 ID 추출
        constexpr int id = PacketProtocolID<MessageType>::value;

        auto invoker = [derived, handler](const Packet& packet) -> bool {
            auto msg = flatbuffers::GetRoot<MessageType>(packet.GetDataPtr());
            if (nullptr == msg)
                return false;

            flatbuffers::Verifier v{
                reinterpret_cast<const uint8_t*>(packet.GetDataPtr()),
                static_cast<flatbuffers::uoffset_t>(packet.GetMessageSize()) };
            if (!msg->Verify(v))
                return false;

            return (derived->*handler)(packet.HostID, *msg);
        };

        _RegisterHandler(id, invoker);
    }

    // -----------------------------------------------------------------------
    // RegisterInnerHandler<Derived>(EInnerProtocol, 멤버함수포인터)
    //   InnerPacket 핸들러 등록.
    //   핸들러 시그니처: bool Derived::Handle_Xxx(InnerPacket::SharedPtr)
    // -----------------------------------------------------------------------
    template <typename DerivedType,
        typename = typename std::enable_if<std::is_base_of<Service, DerivedType>::value>::type>
    void RegisterInnerHandler(EInnerProtocol protocol, bool (DerivedType::* handler)(InnerPacket::SharedPtr))
    {
        DerivedType* derived = static_cast<DerivedType*>(this);
        int id = static_cast<int>(protocol);

        auto invoker = [derived, handler](InnerPacket::SharedPtr pkt) -> bool {
            if (pkt == nullptr) return false;
            return (derived->*handler)(pkt);
        };

        _RegisterInnerHandler(id, invoker);
    }

    void UnRegisterHandler(int messageID)      { mPacketHandler.erase(messageID); }
    void UnRegisterInnerHandler(int protocolID){ mInnerPacketHandler.erase(protocolID); }

private:
    void _RegisterHandler(int id, std::function<bool(const Packet&)> func)
    {
        mPacketHandler.insert_or_assign(id, func);
    }

    void _RegisterInnerHandler(int id, std::function<bool(InnerPacket::SharedPtr)> func)
    {
        mInnerPacketHandler.insert_or_assign(id, func);
    }

    bool _DispatchPacket(const Packet& packet)
    {
        if (auto it = mPacketHandler.find(packet.GetMessageID()); it != mPacketHandler.end())
        {
            if (it->second != nullptr)
                return it->second(packet);
        }
        return false;
    }

    void _PacketProcess()
    {
        mPacketWorkList.clear();
        mPacketQueue.Swap(mPacketWorkList);

        for (auto& pPacket : mPacketWorkList)
        {
            _DispatchPacket(*pPacket);
            pPacket.reset();
        }

        if (mPacketWorkList.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void _InnerPacketProcess()
    {
        mInnerPacketWorkList.clear();
        mInnerPacketQueue.Swap(mInnerPacketWorkList);

        for (auto& pInner : mInnerPacketWorkList)
        {
            if (auto it = mInnerPacketHandler.find(pInner->Protocol); it != mInnerPacketHandler.end())
            {
                if (it->second != nullptr)
                    it->second(pInner);
            }
        }
    }
};
