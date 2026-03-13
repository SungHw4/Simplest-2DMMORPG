#include "stdafx.h"
#include "GameDBService.h"
#include "GameService.h"
#include "DataBase.h"

// DB 조회 결과 extern (DataBase.cpp에서 선언)
extern SQLINTEGER p_id;
extern SQLINTEGER p_x;
extern SQLINTEGER p_y;
extern SQLINTEGER p_hp;
extern SQLINTEGER p_maxhp;
extern SQLINTEGER p_exp;
extern SQLINTEGER p_lv;

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
//   GameService로부터 로그인 DB 조회 요청을 InnerPacket으로 수신.
//   DB 쿼리 수행 후 결과를 InnerPacket으로 GameService에 Push.
// -----------------------------------------------------------------------
bool GameDBService::Handle_LoginRequest(InnerPacket::SharedPtr pInner)
{
    if (pInner == nullptr)
        return false;

    int clientID = pInner->HostID;

    auto* pLoginData = dynamic_cast<LoginInnerData*>(pInner->pData);
    if (pLoginData == nullptr)
        return false;

    // DB 조회
    bool dbResult = Load_DB(pLoginData->name);

    // 결과 InnerPacket 생성 후 GameService에 Push
    auto pResult        = std::make_shared<InnerPacket>();
    pResult->HostID     = clientID;
    pResult->Protocol   = static_cast<int>(EInnerProtocol::DB_LoginResponse);

    auto* pResultData   = new LoginResultData();
    pResultData->success = dbResult;

    if (dbResult)
    {
        pResultData->x      = static_cast<int>(p_x);
        pResultData->y      = static_cast<int>(p_y);
        pResultData->hp     = static_cast<int>(p_hp);
        pResultData->maxhp  = static_cast<int>(p_maxhp);
        pResultData->exp    = static_cast<int>(p_exp);
        pResultData->level  = static_cast<int>(p_lv);
    }

    pResult->pData = pResultData;

    if (mpGameService != nullptr)
        mpGameService->InnerPush(pResult);

    return true;
}
