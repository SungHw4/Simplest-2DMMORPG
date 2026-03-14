#include "stdafx.h"
#include "GameService.h"
#include "GameDBService.h"
#include "Grid.h"

// -----------------------------------------------------------------------
// 생성자: FSCore Service 패턴과 동일하게 RegisterHandler 로 핸들러 등록
// -----------------------------------------------------------------------
GameService::GameService()
{
    // CS 패킷 핸들러 등록
    RegisterHandler<GameService, GameProtocol::CSLogin>
        (&GameService::Handle_Login);
    RegisterHandler<GameService, GameProtocol::CSPlayerMoveRequest>
        (&GameService::Handle_Move);
    RegisterHandler<GameService, GameProtocol::CSPlayerAttackRequest>
        (&GameService::Handle_Attack);
    RegisterHandler<GameService, GameProtocol::CSPlayerChattingRequest>
        (&GameService::Handle_Chatting);
    RegisterHandler<GameService, GameProtocol::CSRandomTeleportRequest>
        (&GameService::Handle_RandomTeleport);

    // InnerPacket 핸들러 등록 (GameDBService -> GameService 결과 수신)
    RegisterInnerHandler<GameService>
        (EInnerProtocol::DB_LoginResponse, &GameService::Handle_DB_LoginResponse);
}

// -----------------------------------------------------------------------
// _SendFB
// -----------------------------------------------------------------------
void GameService::_SendFB(int clientID, std::vector<uint8_t>& framed)
{
    clients[clientID].do_send(static_cast<int>(framed.size()), framed.data());
}

// -----------------------------------------------------------------------
// _SendError
//   에러 발생 시 해당 클라이언트에게 SCIntegrationErrorNotification(500) 전송.
//   모든 핸들러의 SendNak 람다가 이 함수 하나로 통합된다.
//
//   패킷 구조 (FBProtocol::BuildIntegrationError 내부):
//     [4바이트 SC_INTEGRATION_ERROR_NOTIFICATION(500)]
//     [SCIntegrationErrorNotification FlatBuffers]
//       .messageid = originMsgID  (에러가 발생한 원래 요청 패킷 ID)
//       .errorcode = err          (EErrorMsg 에러 코드)
// -----------------------------------------------------------------------
bool GameService::_SendError(int                           clientID,
                              GameProtocol::EPacketProtocol originMsgID,
                              GameProtocol::EErrorMsg        err)
{
    auto framed = FBProtocol::BuildIntegrationError(
        static_cast<int32_t>(originMsgID),
        err);
    _SendFB(clientID, framed);
    return false;
}

// -----------------------------------------------------------------------
// Handle_Login
//   - 이름 검증 후 GameDBService 에 DB 조회 위임 (InnerPacket)
//   - 에러 시: SCIntegrationErrorNotification(CS_LoginRequest, err)
// -----------------------------------------------------------------------
bool GameService::Handle_Login(int clientID, const GameProtocol::CSLogin& msg)
{
    if (!msg.name() || msg.name()->size() == 0)
        return _SendError(clientID,
                          GameProtocol::EPacketProtocol_CS_LoginRequest,
                          GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    // DB 조회 요청을 InnerPacket 으로 GameDBService 에 Push
    auto pInner      = std::make_shared<InnerPacket>();
    pInner->HostID   = clientID;
    pInner->Protocol = static_cast<int>(EInnerProtocol::DB_LoginRequest);
    pInner->pData    = new LoginInnerData(msg.name()->c_str());

    if (mpDBService != nullptr)
        mpDBService->Push(pInner);

    return true;
}

// -----------------------------------------------------------------------
// Handle_DB_LoginResponse
//   GameDBService 로부터 DB 조회 결과를 InnerPacket 으로 수신.
//   성공 시 클라이언트 상태를 ST_INGAME 으로 전환하고 로그인 응답 전송.
// -----------------------------------------------------------------------
bool GameService::Handle_DB_LoginResponse(InnerPacket::SharedPtr pInner)
{
    int clientID = pInner->HostID;

    auto* pResult = dynamic_cast<LoginResultData*>(pInner->pData);
    if (pResult == nullptr || !pResult->data.success)
        return _SendError(clientID,
                          GameProtocol::EPacketProtocol_CS_LoginRequest,
                          GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    const DBPlayerData& d = pResult->data;

    CLIENT& cl  = clients[clientID];
    cl.x        = static_cast<short>(d.x);
    cl.y        = static_cast<short>(d.y);
    cl.level    = static_cast<short>(d.level);
    cl.maxhp    = static_cast<short>(d.maxhp);
    cl.hp       = static_cast<short>(d.hp);
    cl.exp      = static_cast<short>(d.exp);
    cl.dmg      = 10 + (d.level * 3);
    strncpy_s(cl.name, d.name, sizeof(cl.name) - 1);

    cl.state_lock.lock();
    cl._state = ST_INGAME;
    cl.state_lock.unlock();

    // 로그인 성공 응답: SC_LoginResponse(104) 전송
    auto framed = FBProtocol::BuildLoginResponse(GameProtocol::Direction_UP);
    _SendFB(clientID, framed);

    cout << "[GameService] Login OK - clientID=" << clientID
         << " name=" << cl.name << endl;
    return true;
}

// -----------------------------------------------------------------------
// Handle_Move
//   - 방향 검증 후 좌표 이동
//   - 에러 시: SCIntegrationErrorNotification(CS_PlayerMoveRequest, err)
// -----------------------------------------------------------------------
bool GameService::Handle_Move(int clientID, const GameProtocol::CSPlayerMoveRequest& msg)
{
    CLIENT& cl = clients[clientID];
    GameProtocol::Direction dir = msg.direction();

    int old_x = cl.x;
    int old_y = cl.y;
    int x = old_x;
    int y = old_y;

    switch (static_cast<int>(dir))
    {
    case 0: if (y > 0 && obs[y-1][x] == 0) y--; break;                         // UP
    case 1: if (y < (WORLD_HEIGHT - 1) && obs[y+1][x] == 0) y++; break;        // DOWN
    case 2: if (x > 0 && obs[y][x-1] == 0) x--; break;                         // LEFT
    case 3: if (x < (WORLD_WIDTH - 1) && obs[y][x+1] == 0) x++; break;         // RIGHT
    default:
        return _SendError(clientID,
                          GameProtocol::EPacketProtocol_CS_PlayerMoveRequest,
                          GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
    }

    // 그리드 셀 이동 (셀이 바뀌는 경우에만 내부적으로 처리)
    grid_move_player(clientID,
                     cell_x(old_x), cell_y(old_y),
                     cell_x(x),     cell_y(y));

    cl.x = static_cast<short>(x);
    cl.y = static_cast<short>(y);

    // 자신에게 이동 결과 전송
    auto framed = FBProtocol::BuildMoveResponse(dir);
    _SendFB(clientID, framed);

    // 시야 내 다른 플레이어에게 브로드캐스트
    cl.vl.lock();
    std::unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    for (auto other : my_vl)
    {
        if (is_player(other))
        {
            auto f = FBProtocol::BuildMoveResponse(dir);
            clients[other].do_send(static_cast<int>(f.size()), f.data());
        }
    }

    return true;
}

// -----------------------------------------------------------------------
// Handle_Attack
//   - 시야 내 공격 범위에 있는 대상에게 데미지 적용
//   - 에러 시: SCIntegrationErrorNotification(CS_PlayerAttackRequest, err)
// -----------------------------------------------------------------------
bool GameService::Handle_Attack(int clientID, const GameProtocol::CSPlayerAttackRequest& msg)
{
    CLIENT& cl = clients[clientID];
    GameProtocol::Direction dir = msg.direction();

    cl.vl.lock();
    std::unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    for (auto& k : my_vl)
    {
        if (is_attack_range(clientID, k))
        {
            clients[clientID].hp -= clients[k].dmg;
            clients[k].hp        -= clients[clientID].dmg;

            auto framed = FBProtocol::BuildAttackResponse(k, dir, clients[k].hp, clients[k].exp);
            _SendFB(clientID, framed);
        }
    }

    // 자신의 현재 상태 전송
    auto framed = FBProtocol::BuildAttackResponse(clientID, dir,
                      clients[clientID].hp, clients[clientID].exp);
    _SendFB(clientID, framed);

    return true;
}

// -----------------------------------------------------------------------
// Handle_Chatting
//   - 메시지 검증 후 시야 내 브로드캐스트
//   - 에러 시: SCIntegrationErrorNotification(CS_PlayerChattingRequest, err)
// -----------------------------------------------------------------------
bool GameService::Handle_Chatting(int clientID, const GameProtocol::CSPlayerChattingRequest& msg)
{
    if (!msg.message())
        return _SendError(clientID,
                          GameProtocol::EPacketProtocol_CS_PlayerChattingRequest,
                          GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    const char* message = msg.message()->c_str();

    CLIENT& cl = clients[clientID];
    cl.vl.lock();
    std::unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    auto self = FBProtocol::BuildChattingResponse(clientID, message);
    _SendFB(clientID, self);

    for (auto other : my_vl)
    {
        if (is_player(other))
        {
            auto f = FBProtocol::BuildChattingResponse(clientID, message);
            clients[other].do_send(static_cast<int>(f.size()), f.data());
        }
    }

    return true;
}

// -----------------------------------------------------------------------
// Handle_RandomTeleport
//   - 기존 시야 제거 후 랜덤 위치로 이동
//   - 에러는 없으므로 _SendError 미사용
// -----------------------------------------------------------------------
bool GameService::Handle_RandomTeleport(int clientID, const GameProtocol::CSRandomTeleportRequest& msg)
{
    CLIENT& cl = clients[clientID];

    int old_x = cl.x;
    int old_y = cl.y;

    cl.vl.lock();
    std::unordered_set<int> old_vl{ cl.viewlist };
    cl.viewlist.clear();
    cl.vl.unlock();

    for (auto other : old_vl)
    {
        if (!is_npc(other))
        {
            clients[other].vl.lock();
            clients[other].viewlist.erase(clientID);
            clients[other].vl.unlock();
        }
    }

    int new_x, new_y;
    do {
        new_x = rand() % WORLD_WIDTH;
        new_y = rand() % WORLD_HEIGHT;
    } while (obs[new_y][new_x]);

    cl.x = static_cast<short>(new_x);
    cl.y = static_cast<short>(new_y);

    // 그리드 셀 이동
    grid_move_player(clientID,
                     cell_x(old_x), cell_y(old_y),
                     cell_x(new_x), cell_y(new_y));

    auto framed = FBProtocol::BuildMoveResponse(GameProtocol::Direction_UP);
    _SendFB(clientID, framed);

    return true;
}
