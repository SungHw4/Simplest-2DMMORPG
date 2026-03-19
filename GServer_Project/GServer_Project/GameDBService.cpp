#include "stdafx.h"
#include "GameDBService.h"
#include "GameService.h"
#include "DataBase.h"

// -----------------------------------------------------------------------
// 생성자: Redis 연결 시도 + 핸들러 등록
// -----------------------------------------------------------------------
GameDBService::GameDBService()
{
    // Redis 연결 (실패해도 서버는 정상 동작 — SQL fallback)
    try
    {
        mRedis = std::make_unique<sw::redis::Redis>("tcp://127.0.0.1:6379");
        // PING으로 실제 연결 확인
        mRedis->ping();
        std::cout << "[GameDBService] Redis connected (127.0.0.1:6379)\n";
    }
    catch (const sw::redis::Error& e)
    {
        mRedis = nullptr;
        std::cout << "[GameDBService] Redis unavailable: " << e.what()
                  << " → SQL only mode\n";
    }

    RegisterHandler<GameDBService>(
        EInnerProtocol::DB_LoginRequest,
        &GameDBService::Handle_LoginRequest);
}

// -----------------------------------------------------------------------
// Handle_LoginRequest
//   GameService 로부터 로그인 DB 조회 요청을 InnerPacket 으로 수신.
//   Load_DB() 를 호출해 DBPlayerData 를 채운 뒤,
//   LoginResultData 에 담아 GameService 에 InnerPush 한다.
//
//   ※ 기존 extern 전역 변수(p_x, p_y, ...) 방식 완전 제거.
//      Load_DB(name, outData) 가 로컬 변수에 결과를 써넣으므로 스레드 안전.
// -----------------------------------------------------------------------
bool GameDBService::Handle_LoginRequest(InnerPacket::SharedPtr pInner)
{
    if (pInner == nullptr)
        return false;

    int clientID = pInner->HostID;

    auto* pLoginData = dynamic_cast<LoginInnerData*>(pInner->pData);
    if (pLoginData == nullptr)
        return false;

    std::string playerName(pLoginData->name);
    DBPlayerData dbData;
    bool dbResult = false;

    // -----------------------------------------------------------------------
    // 1단계: Redis 캐시 확인
    //   캐시 HIT → SQL 조회 생략
    //   캐시 MISS 또는 Redis 미연결 → SQL 조회
    // -----------------------------------------------------------------------
    if (_CacheGet(playerName, dbData))
    {
        dbResult = true;
        std::cout << "[GameDBService] Cache HIT: " << playerName << "\n";
    }
    else
    {
        // -----------------------------------------------------------------------
        // 2단계: SQL 조회
        // -----------------------------------------------------------------------
        dbResult = Load_DB(pLoginData->name, dbData);

        // 신규 플레이어 → DB에 등록 후 재조회
        if (!dbResult)
        {
            char empty_pwd[] = "";
            Add_DB(pLoginData->name, empty_pwd);
            dbResult = Load_DB(pLoginData->name, dbData);
        }

        // 3단계: 조회 성공 시 Redis에 캐시 저장 (TTL 1시간)
        if (dbResult)
            _CacheSet(playerName, dbData);
    }

    // -----------------------------------------------------------------------
    // 결과 InnerPacket 생성 후 GameService 에 Push
    // -----------------------------------------------------------------------
    auto pResult      = std::make_shared<InnerPacket>();
    pResult->HostID   = clientID;
    pResult->Protocol = static_cast<int>(EInnerProtocol::DB_LoginResponse);

    auto* pResultData         = new LoginResultData();
    pResultData->data         = dbData;
    pResultData->data.success = dbResult;
    pResult->pData            = pResultData;

    if (mpGameService != nullptr)
        mpGameService->InnerPush(pResult);

    return true;
}

// -----------------------------------------------------------------------
// _CacheGet
//   Redis에서 "player:{name}" 키로 캐시 조회.
//   HIT 시 outData를 채우고 true 반환.
//   MISS 또는 Redis 미연결 시 false 반환.
// -----------------------------------------------------------------------
bool GameDBService::_CacheGet(const std::string& name, DBPlayerData& outData)
{
    if (!mRedis) return false;

    try
    {
        auto val = mRedis->get("player:" + name);
        if (!val) return false;          // 키 없음 (MISS)
        return _Deserialize(*val, outData);
    }
    catch (const sw::redis::Error& e)
    {
        std::cout << "[GameDBService] Redis GET error: " << e.what() << "\n";
        return false;
    }
}

// -----------------------------------------------------------------------
// _CacheSet
//   "player:{name}" 키로 DBPlayerData를 직렬화해 Redis에 저장.
//   TTL: 3600초 (1시간) — 접속 후 1시간 뒤 자동 만료
// -----------------------------------------------------------------------
void GameDBService::_CacheSet(const std::string& name, const DBPlayerData& data)
{
    if (!mRedis) return;

    try
    {
        mRedis->set("player:" + name, _Serialize(data),
                    std::chrono::seconds(3600));
    }
    catch (const sw::redis::Error& e)
    {
        std::cout << "[GameDBService] Redis SET error: " << e.what() << "\n";
    }
}

// -----------------------------------------------------------------------
// _Serialize
//   DBPlayerData → "id|x|y|hp|maxhp|exp|level|name" 형식의 문자열
// -----------------------------------------------------------------------
std::string GameDBService::_Serialize(const DBPlayerData& data)
{
    return std::to_string(data.id)    + "|" +
           std::to_string(data.x)     + "|" +
           std::to_string(data.y)     + "|" +
           std::to_string(data.hp)    + "|" +
           std::to_string(data.maxhp) + "|" +
           std::to_string(data.exp)   + "|" +
           std::to_string(data.level) + "|" +
           std::string(data.name);
}

// -----------------------------------------------------------------------
// _Deserialize
//   "id|x|y|hp|maxhp|exp|level|name" 문자열 → DBPlayerData
// -----------------------------------------------------------------------
bool GameDBService::_Deserialize(const std::string& str, DBPlayerData& outData)
{
    try
    {
        std::vector<std::string> tokens;
        std::istringstream ss(str);
        std::string token;
        while (std::getline(ss, token, '|'))
            tokens.push_back(token);

        if (tokens.size() < 8) return false;

        outData.id    = std::stoi(tokens[0]);
        outData.x     = std::stoi(tokens[1]);
        outData.y     = std::stoi(tokens[2]);
        outData.hp    = std::stoi(tokens[3]);
        outData.maxhp = std::stoi(tokens[4]);
        outData.exp   = std::stoi(tokens[5]);
        outData.level = std::stoi(tokens[6]);
        strncpy_s(outData.name, sizeof(outData.name),
                  tokens[7].c_str(), sizeof(outData.name) - 1);
        outData.success = true;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
