#pragma once
#include <memory>

// -----------------------------------------------------------------------
// IInnerData
//   InnerPacket에 담길 데이터의 기반 인터페이스.
//   DB 요청/응답, 서비스 간 이벤트 데이터 등을 파생 클래스로 구현한다.
// -----------------------------------------------------------------------
class IInnerData
{
public:
    IInnerData() = default;
    virtual ~IInnerData() { Release(); }
    virtual void Release() {}
};

// -----------------------------------------------------------------------
// InnerPacket
//   GameService <-> GameDBService 사이의 내부 통신에 사용하는 패킷.
//   네트워크 패킷(Packet)과 달리 직렬화 없이 포인터로 데이터를 전달한다.
//
//   사용 예)
//     // GameService -> GameDBService 로 로그인 DB 조회 요청
//     auto pInner = std::make_shared<InnerPacket>();
//     pInner->HostID   = client_id;
//     pInner->Protocol = static_cast<int>(EInnerProtocol::DB_LoginRequest);
//     pInner->pData    = new LoginInnerData(name);
//     g_DBService.Push(pInner);
//
//     // GameDBService -> GameService 로 결과 콜백
//     auto pInner = std::make_shared<InnerPacket>();
//     pInner->HostID   = hostid;
//     pInner->Protocol = static_cast<int>(EInnerProtocol::DB_LoginResponse);
//     pInner->pData    = new LoginResultData(result, ...);
//     g_GameService.InnerPush(pInner);
// -----------------------------------------------------------------------
class InnerPacket
{
public:
    using SharedPtr = std::shared_ptr<InnerPacket>;

    int          HostID   = 0;   // 요청을 발생시킨 클라이언트 ID
    int          Protocol = 0;   // EInnerProtocol 값 (int 캐스팅)
    IInnerData*  pData    = nullptr;

public:
    InnerPacket() = default;
    virtual ~InnerPacket()
    {
        if (pData != nullptr)
        {
            delete pData;
            pData = nullptr;
        }
    }

    // 복사/이동 금지 (포인터 소유권 문제 방지)
    InnerPacket(const InnerPacket&)            = delete;
    InnerPacket& operator=(const InnerPacket&) = delete;
};

// -----------------------------------------------------------------------
// EInnerProtocol
//   GameService <-> GameDBService 간 사용하는 내부 프로토콜 ID.
//   필요에 따라 추가한다.
// -----------------------------------------------------------------------
enum class EInnerProtocol : int
{
    None = 0,

    // GameService -> GameDBService
    DB_LoginRequest  = 1001,   // 로그인 DB 조회 요청

    // GameDBService -> GameService
    DB_LoginResponse = 2001,   // 로그인 DB 조회 결과
};
