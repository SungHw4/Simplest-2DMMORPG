#include "stdafx.h"
#include "GameService.h"
#include "GameDBService.h"

// -----------------------------------------------------------------------
// 생성자: FSCore Service 패턴과 동일하게 RegisterHandler로 핸들러 등록
// -----------------------------------------------------------------------
GameService::GameService()
{
    // CS 패킷 핸들러 등록
    // FSCore의 RegisterHandler<DerivedType, MessageType>(멤버함수포인터) 패턴
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
// 유틸리티
// -----------------------------------------------------------------------
void GameService::_SendFB(int clientID, std::vector<uint8_t>& framed)
{
    clients[clientID].do_send(static_cast<int>(framed.size()), framed.data());
}

void GameService::_SendNak_Move(int clientID, GameProtocol::EErrorMsg err)
{
    auto framed = FBProtocol::BuildMoveNak(err);
    _SendFB(clientID, framed);
}

void GameService::_SendNak_Attack(int clientID, GameProtocol::EErrorMsg err)
{
    auto framed = FBProtocol::BuildAttackNak(err);
    _SendFB(clientID, framed);
}

void GameService::_SendNak_Chatting(int clientID, GameProtocol::EErrorMsg err)
{
    auto framed = FBProtocol::BuildChattingNak(err);
    _SendFB(clientID, framed);
}

// -----------------------------------------------------------------------
// Handle_Login
//   파싱 검증 후 DB 조회를 GameDBService에 위임 (InnerPacket 사용)
// -----------------------------------------------------------------------
bool GameService::Handle_Login(int clientID, const GameProtocol::CSLogin& msg)
{
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        _SendNak_Chatting(clientID, err);
        return false;
    };

    if (!msg.name() || msg.name()->size() == 0)
        return SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    // DB 조회 요청을 InnerPacket으로 GameDBService에 Push
    auto pInner         = std::make_shared<InnerPacket>();
    pInner->HostID      = clientID;
    pInner->Protocol    = static_cast<int>(EInnerProtocol::DB_LoginRequest);
    pInner->pData       = new LoginInnerData(msg.name()->c_str());

    if (mpDBService != nullptr)
        mpDBService->Push(pInner);

    return true;
}

// -----------------------------------------------------------------------
// Handle_DB_LoginResponse
//   GameDBService 로부터 DB 조회 결과를 InnerPacket으로 수신
// -----------------------------------------------------------------------
bool GameService::Handle_DB_LoginResponse(InnerPacket::SharedPtr pInner)
{
    int clientID = pInner->HostID;

    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        _SendNak_Chatting(clientID, err);
        return false;
    };

    auto* pResult = dynamic_cast<LoginResultData*>(pInner->pData);
    if (pResult == nullptr)
        return SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    if (!pResult->success)
        return SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

    CLIENT& cl   = clients[clientID];
    cl.x         = static_cast<short>(pResult->x);
    cl.y         = static_cast<short>(pResult->y);
    cl.level     = static_cast<short>(pResult->level);
    cl.maxhp     = static_cast<short>(pResult->maxhp);
    cl.hp        = static_cast<short>(pResult->hp);
    cl.exp       = static_cast<short>(pResult->exp);
    cl.dmg       = 10 + (pResult->level * 3);

    cl.state_lock.lock();
    cl._state = ST_INGAME;
    cl.state_lock.unlock();

    // 로그인 성공 응답 전송
    auto framed = FBProtocol::BuildMoveResponse(GameProtocol::Direction_UP);
    _SendFB(clientID, framed);

    cout << "[GameService] Login OK - clientID=" << clientID << endl;
    return true;
}

// -----------------------------------------------------------------------
// Handle_Move
// -----------------------------------------------------------------------
bool GameService::Handle_Move(int clientID, const GameProtocol::CSPlayerMoveRequest& msg)
{
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        _SendNak_Move(clientID, err);
        return false;
    };

    CLIENT& cl = clients[clientID];
    GameProtocol::Direction dir = msg.direction();

    int x = cl.x;
    int y = cl.y;

    switch (static_cast<int>(dir))
    {
    case 0: if (y > 0 && obs[y-1][x] == 0) y--; break;                         // UP
    case 1: if (y < (WORLD_HEIGHT - 1) && obs[y+1][x] == 0) y++; break;        // DOWN
    case 2: if (x > 0 && obs[y][x-1] == 0) x--; break;                         // LEFT
    case 3: if (x < (WORLD_WIDTH - 1) && obs[y][x+1] == 0) x++; break;         // RIGHT
    default:
        return SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
    }

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
// -----------------------------------------------------------------------
bool GameService::Handle_Attack(int clientID, const GameProtocol::CSPlayerAttackRequest& msg)
{
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        _SendNak_Attack(clientID, err);
        return false;
    };

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

    auto framed = FBProtocol::BuildAttackResponse(clientID, dir,
        clients[clientID].hp, clients[clientID].exp);
    _SendFB(clientID, framed);

    return true;
}

// -----------------------------------------------------------------------
// Handle_Chatting
// -----------------------------------------------------------------------
bool GameService::Handle_Chatting(int clientID, const GameProtocol::CSPlayerChattingRequest& msg)
{
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        _SendNak_Chatting(clientID, err);
        return false;
    };

    if (!msg.message())
        return SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);

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
// -----------------------------------------------------------------------
bool GameService::Handle_RandomTeleport(int clientID, const GameProtocol::CSRandomTeleportRequest& msg)
{
    CLIENT& cl = clients[clientID];

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

    auto framed = FBProtocol::BuildMoveResponse(GameProtocol::Direction_UP);
    _SendFB(clientID, framed);

    return true;
}
