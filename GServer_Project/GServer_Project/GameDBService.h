#pragma once
#include "InnerPacket.h"
#include "ObjectQueue.h"
#include "SStruct.h"
#include "game_protocol_generated.h"

#include <functional>
#include <unordered_map>
#include <deque>
#include <thread>

// 전방 선언
class GameService;

// -----------------------------------------------------------------------
// LoginInnerData
//   GameService -> GameDBService 로그인 요청에 담기는 데이터.
//   SStruct.h 의 DBPlayerData 와 짝을 이룬다.
// -----------------------------------------------------------------------
struct LoginInnerData : public IInnerData
{
    char name[50] = {};
    explicit LoginInnerData(const char* n)
    {
        strncpy_s(name, sizeof(name), n, sizeof(name) - 1);
    }
};

// -----------------------------------------------------------------------
// LoginResultData
//   GameDBService -> GameService 로그인 결과에 담기는 데이터.
//   DBPlayerData 를 그대로 포함한다.
// -----------------------------------------------------------------------
struct LoginResultData : public IInnerData
{
    DBPlayerData data;   // success 플래그 포함
};

// -----------------------------------------------------------------------
// GameDBService
//   FSCore DatabaseService 패턴을 적용한 DB 전용 처리 서비스.
//
//   역할:
//     - InnerPacket 으로 DB 작업 요청을 수신
//     - DB 쿼리 수행 후 결과를 InnerPacket 으로 GameService 에 전달
//
//   흐름:
//     GameService -> Push(InnerPacket)
//       -> GameDBService 스레드: RegisterHandler 로 등록된 핸들러 호출
//         -> DB 쿼리 수행 (Load_DB → DBPlayerData)
//           -> GameService.InnerPush(InnerPacket) 결과 전달
// -----------------------------------------------------------------------
class GameDBService
{
public:
    GameDBService();
    ~GameDBService() { Exit(); }

    void SetGameService(GameService* pGameService) { mpGameService = pGameService; }

    // InnerPacket 작업 요청 Push
    void Push(InnerPacket::SharedPtr pInner)
    {
        if (pInner != nullptr)
            mWorkQueue.Push(pInner);
    }

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

    size_t GetWorkQueueCount() { return mWorkQueue.Count(); }

private:
    void Run()
    {
        while (!mShouldExit)
        {
            _WorkProcess();
        }
    }

    void _WorkProcess()
    {
        mWorkList.clear();
        mWorkQueue.Swap(mWorkList);

        for (auto& pInner : mWorkList)
        {
            if (auto it = mHandlers.find(pInner->Protocol); it != mHandlers.end())
            {
                if (it->second != nullptr)
                    it->second(pInner);
            }
        }

        if (mWorkList.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // -----------------------------------------------------------------------
    // RegisterHandler<Derived>(EInnerProtocol, 멤버함수포인터)
    //   DB 작업 핸들러 등록 (FSCore DatabaseService 패턴 동일)
    // -----------------------------------------------------------------------
    template <typename DerivedType,
        typename = typename std::enable_if<std::is_base_of<GameDBService, DerivedType>::value>::type>
    void RegisterHandler(EInnerProtocol protocol, bool (DerivedType::* handler)(InnerPacket::SharedPtr))
    {
        DerivedType* derived = static_cast<DerivedType*>(this);
        int id = static_cast<int>(protocol);

        auto invoker = [derived, handler](InnerPacket::SharedPtr pkt) -> bool {
            if (pkt == nullptr) return false;
            return (derived->*handler)(pkt);
        };

        mHandlers.insert_or_assign(id, invoker);
    }

    // -----------------------------------------------------------------------
    // DB 작업 핸들러
    // -----------------------------------------------------------------------
    bool Handle_LoginRequest(InnerPacket::SharedPtr pInner);

private:
    // -----------------------------------------------------------------------
    // Redis 캐시 헬퍼
    //   mRedis가 nullptr이면 Redis 미연결 상태 → SQL 직접 조회로 fallback
    // -----------------------------------------------------------------------
    bool         _CacheGet(const std::string& name, DBPlayerData& outData);
    void         _CacheSet(const std::string& name, const DBPlayerData& data);
    std::string  _Serialize(const DBPlayerData& data);
    bool         _Deserialize(const std::string& str, DBPlayerData& outData);

private:
    GameService* mpGameService = nullptr;

    bool        mShouldExit = false;
    std::thread mThread;

    // Redis 연결 (연결 실패 시 nullptr → SQL fallback)
    std::unique_ptr<sw::redis::Redis> mRedis;

    ObjectQueue<InnerPacket::SharedPtr>                              mWorkQueue;
    std::unordered_map<int, std::function<bool(InnerPacket::SharedPtr)>> mHandlers;
    std::deque<InnerPacket::SharedPtr>                               mWorkList;
};
