#include "stdafx.h"
#include "GameDBService.h"
#include "GameService.h"
#include "DataBase.h"

// -----------------------------------------------------------------------
// 생성자: FSCore DatabaseService 패턴과 동일하게 RegisterHandler로 핸들러 등록
// -----------------------------------------------------------------------
GameDBService::GameDBService()
{
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

    // -----------------------------------------------------------------------
    // DB 조회 — 결과는 DBPlayerData 구조체로 반환 (전역 변수 없음)
    // -----------------------------------------------------------------------
    DBPlayerData dbData;
    bool dbResult = Load_DB(pLoginData->name, dbData);

    // -----------------------------------------------------------------------
    // 결과 InnerPacket 생성 후 GameService 에 Push
    // -----------------------------------------------------------------------
    auto pResult       = std::make_shared<InnerPacket>();
    pResult->HostID    = clientID;
    pResult->Protocol  = static_cast<int>(EInnerProtocol::DB_LoginResponse);

    auto* pResultData         = new LoginResultData();
    pResultData->data         = dbData;          // DBPlayerData 통째로 복사
    pResultData->data.success = dbResult;
    pResult->pData            = pResultData;

    if (mpGameService != nullptr)
        mpGameService->InnerPush(pResult);

    return true;
}
