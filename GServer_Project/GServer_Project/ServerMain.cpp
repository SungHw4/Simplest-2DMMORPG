#include "stdafx.h"
#include "DataBase.h"
#include "GameService.h"
#include "GameDBService.h"
#include "GameObjects.h"
#include "Grid.h"
#include "ExpOverPool.h"

HANDLE g_h_iocp;
SOCKET g_s_socket;
mutex LuaLock;

// -----------------------------------------------------------------------
// 전역 서비스 인스턴스
// -----------------------------------------------------------------------
GameService    g_GameService;
GameDBService  g_DBService;

BOOL obs[WORLD_HEIGHT][WORLD_WIDTH];

concurrency::concurrent_priority_queue<timer_event> timer_queue;

// -----------------------------------------------------------------------
// 전역 오브젝트 배열 (Player / NPC 분리)
//   Player: 소켓/IOCP 수신 필드 포함  (MAX_USER개)
//   NPC   : Lua AI 필드 포함           (MAX_NPC개)
// -----------------------------------------------------------------------
std::array<Player, MAX_USER> g_players;
std::array<NPC,    MAX_NPC>  g_npcs;

void do_npc_move(int npc_id, int target, std::chrono::seconds time = 1s);
void npcmove(int npc_id);

// -----------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------
void lock_two_viewlists(int id1, int id2, std::function<void()> callback)
{
    if (id1 == id2) {
        lock_guard<mutex> lock(GetObject(id1)->vl);
        callback();
    } else if (id1 < id2) {
        lock_guard<mutex> lock1(GetObject(id1)->vl);
        lock_guard<mutex> lock2(GetObject(id2)->vl);
        callback();
    } else {
        lock_guard<mutex> lock1(GetObject(id2)->vl);
        lock_guard<mutex> lock2(GetObject(id1)->vl);
        callback();
    }
}

bool is_near(int a, int b)
{
    if (RANGE < abs(GetObject(a)->x - GetObject(b)->x)) return false;
    if (RANGE < abs(GetObject(a)->y - GetObject(b)->y)) return false;
    return true;
}

bool is_attack_range(int a, int b)
{
    if (2 < abs(GetObject(a)->x - GetObject(b)->x)) return false;
    if (2 < abs(GetObject(a)->y - GetObject(b)->y)) return false;
    return true;
}

bool is_npc(int id)
{
    return (id >= NPC_ID_START) && (id <= NPC_ID_END);
}

bool is_player(int id)
{
    return (id >= 0) && (id < MAX_USER);
}

int get_new_id()
{
    static mutex id_allocation_mutex;
    lock_guard<mutex> lock(id_allocation_mutex);
    for (int i = 0; i < MAX_USER; ++i) {
        if (ST_FREE == g_players[i]._state) {
            g_players[i]._state = ST_ACCEPT;
            return i;
        }
    }
    cout << "Maximum Number of Clients Overflow!!\n";
    return -1;
}

// -----------------------------------------------------------------------
// SC → Client 전송 헬퍼 (Player에만 전송)
// -----------------------------------------------------------------------
void send_fb_packet(int c_id, std::vector<uint8_t>& framed)
{
    GetPlayer(c_id)->do_send(static_cast<int>(framed.size()), framed.data());
}

void send_move_response(int c_id, GameProtocol::Direction dir)
{
    auto framed = FBProtocol::BuildMoveResponse(dir);
    send_fb_packet(c_id, framed);
}

void send_attack_response(int c_id, int target_id,
                          GameProtocol::Direction dir, int hp, int exp)
{
    auto framed = FBProtocol::BuildAttackResponse(target_id, dir, hp, exp);
    send_fb_packet(c_id, framed);
}

void send_chatting_response(int c_id, int sender_id, const char* message)
{
    auto framed = FBProtocol::BuildChattingResponse(sender_id, message);
    send_fb_packet(c_id, framed);
}

// -----------------------------------------------------------------------
// Disconnect
// -----------------------------------------------------------------------
void Disconnect(int c_id)
{
    if (!is_player(c_id)) return;

    Player& cl = *GetPlayer(c_id);

    grid_remove_player(c_id, cl.x, cl.y);
    UpdatePlayerOnDB(c_id, cl);
    g_DBService.InvalidateCache(cl.name);

    cl.vl.lock();
    unordered_set<int> my_vl = cl.viewlist;
    cl.vl.unlock();

    for (auto& other_id : my_vl) {
        BaseObject* target = GetObject(other_id);
        if (is_npc(target->_id)) continue;
        if (ST_INGAME != target->_state) continue;
        target->vl.lock();
        if (0 != target->viewlist.count(c_id)) {
            target->viewlist.erase(c_id);
        }
        target->vl.unlock();
    }

    cl.state_lock.lock();
    closesocket(cl._socket);
    cl._state = ST_FREE;
    cl.state_lock.unlock();
}

void Activate_Player_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = g_ExpOverPool.Acquire();
    ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
    exp_over->_comp_op = OP_PLAYER_MOVE;
    exp_over->_target  = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

void Activate_NPC_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = g_ExpOverPool.Acquire();
    ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
    exp_over->_comp_op = OP_NPC_MOVE;
    exp_over->_target  = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

// -----------------------------------------------------------------------
// CS 패킷 브릿지 함수 (GameService 로 위임)
// -----------------------------------------------------------------------
void handle_login(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    auto pPacket = Packet::New(client_id, CS_LOGIN_REQUEST, fb_data, fb_size);
    g_GameService.Push(pPacket);
}

void handle_move(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    auto pPacket = Packet::New(client_id, CS_PLAYER_MOVE_REQUEST, fb_data, fb_size);
    g_GameService.Push(pPacket);
}

void handle_attack(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    auto pPacket = Packet::New(client_id, CS_PLAYER_ATTACK_REQUEST, fb_data, fb_size);
    g_GameService.Push(pPacket);
}

void handle_chatting(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    auto pPacket = Packet::New(client_id, CS_PLAYER_CHATTING_REQUEST, fb_data, fb_size);
    g_GameService.Push(pPacket);
}

void handle_random_teleport(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    auto pPacket = Packet::New(client_id, CS_RANDOM_TELEPORT_REQUEST, fb_data, fb_size);
    g_GameService.Push(pPacket);
}

// -----------------------------------------------------------------------
// 전역 핸들러 매니저
// -----------------------------------------------------------------------
PacketHandlerManager g_packet_handler;

void RegisterAllHandlers()
{
    g_packet_handler.RegisterHandler(CS_LOGIN_REQUEST,          handle_login);
    g_packet_handler.RegisterHandler(CS_PLAYER_MOVE_REQUEST,    handle_move);
    g_packet_handler.RegisterHandler(CS_PLAYER_ATTACK_REQUEST,  handle_attack);
    g_packet_handler.RegisterHandler(CS_PLAYER_CHATTING_REQUEST, handle_chatting);
    g_packet_handler.RegisterHandler(CS_RANDOM_TELEPORT_REQUEST, handle_random_teleport);
}

// -----------------------------------------------------------------------
// Worker thread
// -----------------------------------------------------------------------
void worker()
{
    for (;;) {
        DWORD num_byte;
        LONG64 iocp_key;
        WSAOVERLAPPED* p_over;
        BOOL ret = GetQueuedCompletionStatus(
            g_h_iocp, &num_byte, (PULONG_PTR)&iocp_key, &p_over, INFINITE);

        int client_id      = static_cast<int>(iocp_key);
        EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(p_over);

        if (FALSE == ret) {
            int err_no = WSAGetLastError();
            cout << "GQCS Error : ";
            error_display(err_no);
            cout << endl;
            Disconnect(client_id);
            if (exp_over->_comp_op == OP_SEND)
                g_ExpOverPool.Release(exp_over);
            continue;
        }

        switch (exp_over->_comp_op) {
        case OP_RECV: {
            if (num_byte == 0) {
                Disconnect(client_id);
                continue;
            }

            Player& cl  = *GetPlayer(client_id);
            int   total = num_byte + cl._prev_size;
            uint8_t* buf = exp_over->_net_buf;

            while (total >= 8) {
                int32_t  messege_id = FBProtocol::ReadMessegeId(buf);
                uint32_t fb_size    =
                    static_cast<uint32_t>(buf[4])        |
                    (static_cast<uint32_t>(buf[5]) << 8)  |
                    (static_cast<uint32_t>(buf[6]) << 16) |
                    (static_cast<uint32_t>(buf[7]) << 24);
                uint32_t full_size = 4 + 4 + fb_size;

                if (total < static_cast<int>(full_size)) break;

                const uint8_t* fb_data = buf + 8;
                g_packet_handler.Dispatch(client_id, messege_id, fb_data, fb_size);

                total -= static_cast<int>(full_size);
                buf   += full_size;
            }

            cl._prev_size = (total > 0) ? total : 0;
            if (total > 0)
                memcpy(exp_over->_net_buf, buf, total);

            cl.do_recv();
            break;
        }
        case OP_SEND: {
            if (num_byte != exp_over->_wsa_buf.len)
                Disconnect(client_id);
            g_ExpOverPool.Release(exp_over);
            break;
        }
        case OP_ACCEPT: {
            SOCKET c_socket = *(reinterpret_cast<SOCKET*>(exp_over->_net_buf));
            int new_id = get_new_id();
            if (-1 == new_id) {
                cout << "Maximum user overflow. Accept aborted.\n";
            } else {
                Player& cl        = *GetPlayer(new_id);
                cl.x              = rand() % WORLD_WIDTH;
                cl.y              = rand() % WORLD_HEIGHT;
                cl._id            = new_id;
                cl._prev_size     = 0;
                cl._recv_over._comp_op        = OP_RECV;
                cl._recv_over._wsa_buf.buf    =
                    reinterpret_cast<char*>(cl._recv_over._net_buf);
                cl._recv_over._wsa_buf.len    = sizeof(cl._recv_over._net_buf);
                ZeroMemory(&cl._recv_over._wsa_over, sizeof(cl._recv_over._wsa_over));
                cl._socket = c_socket;

                grid_add_player(new_id, cl.x, cl.y);

                CreateIoCompletionPort(
                    reinterpret_cast<HANDLE>(c_socket), g_h_iocp, new_id, 0);
                cl.do_recv();
            }

            ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
            c_socket = WSASocket(
                AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
            *(reinterpret_cast<SOCKET*>(exp_over->_net_buf)) = c_socket;
            AcceptEx(g_s_socket, c_socket, exp_over->_net_buf + 8, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                NULL, &exp_over->_wsa_over);
            break;
        }
        case OP_NPC_MOVE: {
            NPC* npc = GetNPC(client_id);
            npc->state_lock.lock();
            if (npc->get_state() != ST_INGAME) {
                npc->state_lock.unlock();
                npc->set_active(false);
                g_ExpOverPool.Release(exp_over);
                break;
            }
            npc->state_lock.unlock();

            if (exp_over->_target == -1) {
                npcmove(client_id);
                g_ExpOverPool.Release(exp_over);
                break;
            }

            npc->Lua_Lock.lock();
            lua_State* L = npc->L;
            lua_getglobal(L, "event_NPC_move");
            lua_pushnumber(L, exp_over->_target);
            int err = lua_pcall(L, 1, 1, 0);
            if (err != 0) {
                cout << "[Lua] event_NPC_move ERR: "
                     << lua_tostring(L, -1) << endl;
                lua_pop(L, 1);
                npc->Lua_Lock.unlock();
                g_ExpOverPool.Release(exp_over);
                break;
            }
            bool should_move = lua_toboolean(L, -1);
            lua_pop(L, 1);
            npc->Lua_Lock.unlock();

            if (should_move) do_npc_move(client_id, exp_over->_target);
            g_ExpOverPool.Release(exp_over);
            break;
        }
        case OP_PLAYER_MOVE: {
            NPC* npc = GetNPC(client_id);
            npc->Lua_Lock.lock();
            lua_State* L = npc->L;
            lua_getglobal(L, "event_player_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 0, 0);
            if (error != 0)
                cout << lua_tostring(L, -1) << endl;
            npc->Lua_Lock.unlock();
            g_ExpOverPool.Release(exp_over);
            break;
        }
        }
    }
}

// -----------------------------------------------------------------------
// Lua API
// -----------------------------------------------------------------------
int API_SendMessage(lua_State* L)
{
    int   my_id   = (int)lua_tointeger(L, -3);
    int   user_id = (int)lua_tointeger(L, -2);
    char* mess    = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);
    send_chatting_response(user_id, my_id, mess);
    return 0;
}

int API_get_x(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushnumber(L, GetObject(user_id)->x.load());
    return 1;
}

int API_get_y(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushnumber(L, GetObject(user_id)->y.load());
    return 1;
}

int API_Touch_Message(lua_State* L)
{
    int   my_id   = (int)lua_tointeger(L, -3);
    int   user_id = (int)lua_tointeger(L, -2);
    char* mess    = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);
    this_thread::sleep_for(1s);
    for (int i = 1; i < 4; ++i) {
        timer_event ev;
        ev.obj_id     = my_id;
        ev.target_id  = user_id;
        ev.start_time = chrono::system_clock::now() + chrono::seconds(i);
        ev.ev         = EVENT_NPC_MOVE;
        timer_queue.push(ev);
    }
    this_thread::sleep_for(3s);
    send_chatting_response(user_id, my_id, mess);
    return 0;
}

// -----------------------------------------------------------------------
// NPC 초기화
// -----------------------------------------------------------------------
void Initialize_NPC()
{
    for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
        NPC* npc = GetNPC(i);

        npc->npc_type = rand() % 2 + 2;
        if (npc->npc_type == 2) sprintf_s(npc->name, "DOG%d", i);
        if (npc->npc_type == 3) sprintf_s(npc->name, "CAT%d", i);

        npc->x          = rand() % WORLD_WIDTH;
        npc->y          = rand() % WORLD_HEIGHT;
        npc->_id        = i;
        npc->_state     = ST_INGAME;
        npc->_is_active = false;
        npc->exp        = 100;
        npc->hp         = 100;
        npc->maxhp      = 100;
        npc->dmg        = 10 + (1 * 3);

        grid_add_npc(i, npc->x, npc->y);

        lua_State* lua = npc->L = luaL_newstate();
        luaL_openlibs(lua);
        luaL_loadfile(lua, "monster.lua");
        lua_pcall(lua, 0, 0, 0);
        lua_getglobal(lua, "set_uid");
        lua_pushnumber(lua, i);
        lua_pushnumber(lua, npc->x.load());
        lua_pushnumber(lua, npc->y.load());
        lua_pcall(lua, 3, 3, 0);
        lua_pop(lua, 3);

        lua_register(lua, "API_get_x",       API_get_x);
        lua_register(lua, "API_get_y",       API_get_y);
        lua_register(lua, "API_SendMessage", API_SendMessage);
    }
}

// -----------------------------------------------------------------------
// 장애물 초기화
// -----------------------------------------------------------------------
void Initialize_obstacle()
{
    srand(2);
    for (int i = 0; i < WORLD_HEIGHT; ++i) {
        for (int j = 0; j < WORLD_WIDTH; ++j) {
            obs[i][j] = rand() % 4;
            if (obs[i][j]) obs[i][j] = 0;
            else           obs[i][j] = 1;
        }
    }
}

// -----------------------------------------------------------------------
// NPC 이동 (랜덤 방랑)
// -----------------------------------------------------------------------
void npcmove(int npc_id)
{
    NPC* npc = GetNPC(npc_id);

    int old_cx = cell_x(npc->x);
    int old_cy = cell_y(npc->y);

    unordered_set<int> old_near;
    {
        unordered_set<int> cands;
        grid_get_near_players(old_cx, old_cy, cands);
        for (int id : cands)
            if (GetPlayer(id)->_state == ST_INGAME && is_near(npc_id, id))
                old_near.insert(id);
    }

    auto& x = npc->x;
    auto& y = npc->y;
    switch (rand() % 4) {
    case 0: if (y > 0 && (obs[y-1][x] == 0)) y--; break;
    case 1: if (y < (WORLD_HEIGHT - 1) && (obs[y+1][x] == 0)) y++; break;
    case 2: if (x > 0 && (obs[y][x-1] == 0)) x--; break;
    case 3: if (x < (WORLD_WIDTH - 1) && (obs[y][x+1] == 0)) x++; break;
    }

    int new_cx = cell_x(x), new_cy = cell_y(y);
    grid_move_npc(npc_id, old_cx, old_cy, new_cx, new_cy);

    unordered_set<int> new_near;
    {
        unordered_set<int> cands;
        grid_get_near_players(new_cx, new_cy, cands);
        for (int id : cands)
            if (GetPlayer(id)->_state == ST_INGAME && is_near(npc_id, id))
                new_near.insert(id);
    }

    for (int pl : new_near) {
        if (old_near.count(pl) == 0) {
            GetPlayer(pl)->vl.lock();
            GetPlayer(pl)->viewlist.insert(npc_id);
            GetPlayer(pl)->vl.unlock();
        }
        send_move_response(pl, GameProtocol::Direction_UP);
    }

    for (int pl : old_near) {
        if (new_near.count(pl) == 0) {
            GetPlayer(pl)->vl.lock();
            GetPlayer(pl)->viewlist.erase(npc_id);
            GetPlayer(pl)->vl.unlock();
        }
    }

    timer_event ev;
    ev.obj_id     = npc_id;
    ev.target_id  = -1;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev         = EVENT_NPC_MOVE;
    timer_queue.push(ev);
}

// -----------------------------------------------------------------------
// NPC 이동 (플레이어 추적)
// -----------------------------------------------------------------------
void do_npc_move(int npc_id, int target, std::chrono::seconds time)
{
    NPC*   npc  = GetNPC(npc_id);
    Player* tgt = GetPlayer(target);

    int old_cx = cell_x(npc->x);
    int old_cy = cell_y(npc->y);

    unordered_set<int> old_viewlist;
    {
        unordered_set<int> candidates;
        grid_get_near_players(old_cx, old_cy, candidates);
        for (int id : candidates)
            if (GetPlayer(id)->_state == ST_INGAME && is_near(npc_id, id))
                old_viewlist.insert(id);
    }

    auto& x   = npc->x;
    auto& y   = npc->y;
    short t_x = tgt->x.load();
    short t_y = tgt->y.load();

    if      (t_x != x) x += (t_x > x) ? 1 : -1;
    else if (t_y != y) y += (t_y > y) ? 1 : -1;

    int new_cx = cell_x(x);
    int new_cy = cell_y(y);
    grid_move_npc(npc_id, old_cx, old_cy, new_cx, new_cy);

    unordered_set<int> new_viewlist;
    {
        unordered_set<int> candidates;
        grid_get_near_players(new_cx, new_cy, candidates);
        for (int id : candidates)
            if (GetPlayer(id)->_state == ST_INGAME && is_near(npc_id, id))
                new_viewlist.insert(id);
    }

    for (auto pl : new_viewlist) {
        if (old_viewlist.count(pl) == 0) {
            GetPlayer(pl)->vl.lock();
            GetPlayer(pl)->viewlist.insert(npc_id);
            GetPlayer(pl)->vl.unlock();
        }
        send_move_response(pl, GameProtocol::Direction_UP);
    }

    for (auto pl : old_viewlist) {
        if (new_viewlist.count(pl) == 0) {
            GetPlayer(pl)->vl.lock();
            GetPlayer(pl)->viewlist.erase(npc_id);
            GetPlayer(pl)->vl.unlock();
        }
    }

    npc->state_lock.lock();
    if (npc->get_state() != ST_INGAME) {
        npc->state_lock.unlock();
        return;
    }
    npc->state_lock.unlock();

    timer_event ev;
    ev.obj_id     = npc_id;
    ev.target_id  = target;
    ev.start_time = chrono::system_clock::now() + time;
    ev.ev         = EVENT_NPC_MOVE;
    timer_queue.push(ev);
}

// -----------------------------------------------------------------------
// 타이머 스레드
// -----------------------------------------------------------------------
void do_timer()
{
    chrono::system_clock::duration dura = 0ms;
    const chrono::milliseconds waittime = 10ms;
    timer_event temp;
    bool temp_bool = false;
    while (true) {
        if (temp_bool) {
            temp_bool = false;
            EXP_OVER* ex_over = g_ExpOverPool.Acquire();
            ZeroMemory(&ex_over->_wsa_over, sizeof(ex_over->_wsa_over));
            ex_over->_comp_op = OP_NPC_MOVE;
            ex_over->_target  = temp.target_id;
            PostQueuedCompletionStatus(
                g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = g_ExpOverPool.Acquire();
                ZeroMemory(&ex_over->_wsa_over, sizeof(ex_over->_wsa_over));
                ex_over->_comp_op = OP_NPC_MOVE;
                ex_over->_target  = ev.target_id;
                PostQueuedCompletionStatus(
                    g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);
            } else if (dura <= waittime) {
                temp      = ev;
                temp_bool = true;
                break;
            } else {
                timer_queue.push(ev);
                break;
            }
        }
        if (dura > 0ms)
            this_thread::sleep_for(dura);
    }
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main()
{
    grid_initialize();
    Initialize_DB();
    Initialize_NPC();
    Initialize_obstacle();
    RegisterAllHandlers();

    constexpr size_t POOL_SIZE = 5000 * 30 * 2;
    g_ExpOverPool.Init(POOL_SIZE);

    g_DBService.SetGameService(&g_GameService);
    g_GameService.SetDBService(&g_DBService);
    g_DBService.StartThread();
    g_GameService.StartThread();
    cout << "[Service] GameService & GameDBService threads started" << endl;

    wcout.imbue(locale("korean"));

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    if (g_s_socket == INVALID_SOCKET) {
        cout << "[ERROR] WSASocket failed: " << WSAGetLastError() << endl;
        return -1;
    }

    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(g_s_socket,
               reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        cout << "[ERROR] bind failed: " << WSAGetLastError() << endl;
        return -1;
    }
    if (listen(g_s_socket, SOMAXCONN) != 0) {
        cout << "[ERROR] listen failed: " << WSAGetLastError() << endl;
        return -1;
    }
    cout << "[OK] Listening on 0.0.0.0:" << SERVER_PORT << endl;

    g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 0, 0);

    SOCKET   c_socket = WSASocket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    EXP_OVER accept_ex;
    *(reinterpret_cast<SOCKET*>(&accept_ex._net_buf)) = c_socket;
    ZeroMemory(&accept_ex._wsa_over, sizeof(accept_ex._wsa_over));
    accept_ex._comp_op = OP_ACCEPT;

    AcceptEx(g_s_socket, c_socket, accept_ex._net_buf + 8, 0,
             sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
             NULL, &accept_ex._wsa_over);
    cout << "Accept Called\n";

    for (int i = 0; i < MAX_USER; ++i)
        g_players[i]._id = i;
    cout << "Creating Worker Threads\n";

    vector<thread> worker_threads;
    thread timer_thread{ do_timer };
    for (int i = 0; i < 16; ++i)
        worker_threads.emplace_back(worker);
    for (auto& th : worker_threads)
        th.join();

    timer_thread.join();

    // 종료 시 인게임 플레이어 저장
    for (auto& pl : g_players) {
        if (ST_INGAME == pl._state)
            Disconnect(pl._id);
    }
    closesocket(g_s_socket);
    WSACleanup();
    Disconnect_DB();
}
