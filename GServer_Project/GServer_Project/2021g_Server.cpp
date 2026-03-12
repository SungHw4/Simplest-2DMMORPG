#pragma once

#include "stdafx.h"
#include "DataBase.h"

HANDLE g_h_iocp;
SOCKET g_s_socket;
mutex LuaLock;

BOOL obs[WORLD_HEIGHT][WORLD_WIDTH];

extern SQLINTEGER p_id;
extern SQLINTEGER p_x;
extern SQLINTEGER p_y;
extern SQLINTEGER p_hp;
extern SQLINTEGER p_maxhp;
extern SQLINTEGER p_exp;
extern SQLINTEGER p_lv;

concurrency::concurrent_priority_queue<timer_event> timer_queue;

void do_npc_move(int npc_id, int target, std::chrono::seconds time = 1s);
void npcmove(int npc_id);

array<CLIENT, MAX_USER + MAX_NPC> clients;

// -----------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------
void lock_two_viewlists(int id1, int id2, std::function<void()> callback)
{
    if (id1 == id2) {
        lock_guard<mutex> lock(clients[id1].vl);
        callback();
    } else if (id1 < id2) {
        lock_guard<mutex> lock1(clients[id1].vl);
        lock_guard<mutex> lock2(clients[id2].vl);
        callback();
    } else {
        lock_guard<mutex> lock1(clients[id2].vl);
        lock_guard<mutex> lock2(clients[id1].vl);
        callback();
    }
}

bool is_near(int a, int b)
{
    if (RANGE < abs(clients[a].x - clients[b].x)) return false;
    if (RANGE < abs(clients[a].y - clients[b].y)) return false;
    return true;
}

bool is_attack_range(int a, int b)
{
    if (2 < abs(clients[a].x - clients[b].x)) return false;
    if (2 < abs(clients[a].y - clients[b].y)) return false;
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
        if (ST_FREE == clients[i]._state) {
            clients[i]._state = ST_ACCEPT;
            return i;
        }
    }
    cout << "Maximum Number of Clients Overflow!!\n";
    return -1;
}

// -----------------------------------------------------------------------
// Helper: FlatBuffers 프레임 버퍼를 클라이언트에게 전송
//   framed = [4바이트 messegeid][FlatBuffers 데이터]
// -----------------------------------------------------------------------
void send_fb_packet(int c_id, std::vector<uint8_t>& framed)
{
    clients[c_id].do_send(static_cast<int>(framed.size()), framed.data());
}

// -----------------------------------------------------------------------
// SC → Client 전송 함수들
// -----------------------------------------------------------------------

// 이동 결과 전송 (SC_PlayerMoveResponse, 202)
void send_move_response(int c_id, GameProtocol::Direction dir)
{
    auto framed = FBProtocol::BuildMoveResponse(dir);
    send_fb_packet(c_id, framed);
}

// 공격 결과 전송 (SC_PlayerAttackResponse, 206)
void send_attack_response(int c_id, int target_id,
                          GameProtocol::Direction dir, int hp, int exp)
{
    auto framed = FBProtocol::BuildAttackResponse(target_id, dir, hp, exp);
    send_fb_packet(c_id, framed);
}

// 채팅 전송 (SC_PlayerChattingResponse, 302)
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
    CLIENT& cl = clients[c_id];
    UpdatePlayerOnDB(c_id, clients[c_id]);
    cl.vl.lock();
    unordered_set<int> my_vl = cl.viewlist;
    cl.vl.unlock();
    for (auto& other_id : my_vl) {
        CLIENT& target = clients[other_id];
        if (true == is_npc(target._id)) continue;
        if (ST_INGAME != target._state) continue;
        target.vl.lock();
        if (0 != target.viewlist.count(c_id)) {
            target.viewlist.erase(c_id);
            target.vl.unlock();
            // 시야에서 제거 알림: 이동 응답(빈 방향)으로 대체
            // (향후 SCRemoveObject 패킷 추가 권장)
        } else {
            target.vl.unlock();
        }
    }
    clients[c_id].state_lock.lock();
    closesocket(clients[c_id]._socket);
    clients[c_id]._state = ST_FREE;
    clients[c_id].state_lock.unlock();
}

void Activate_Player_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = new EXP_OVER;
    exp_over->_comp_op = OP_PLAYER_MOVE;
    exp_over->_target  = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

void Activate_NPC_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = new EXP_OVER;
    exp_over->_comp_op = OP_NPC_MOVE;
    exp_over->_target  = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

// -----------------------------------------------------------------------
// CS → Server 패킷 핸들러 함수 (EPacketProtocol 기반)
// -----------------------------------------------------------------------

// CS_LoginRequest (101): 로그인
void handle_login(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    // NAK 전송 람다: EErrorMsg를 채팅 응답(id=에러코드, message=에러이름)으로 전달 후 false 반환
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        auto framed = FBProtocol::BuildChattingNak(err);
        send_fb_packet(client_id, framed);
        return false;
    };

    const GameProtocol::CSLogin* login = FBProtocol::ParseCSLogin(fb_data, fb_size);
    if (!login || !login->name()) {
        cout << "[handle_login] Invalid CSLogin from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    CLIENT& cl = clients[client_id];
    strncpy_s(cl.name, login->name()->c_str(), MAX_NAME_SIZE - 1);

    if (Load_DB(cl.name)) {
        cl.x     = static_cast<short>(p_x);
        cl.y     = static_cast<short>(p_y);
        cl.level = static_cast<short>(p_lv);
        cl.maxhp = static_cast<short>(p_maxhp);
        cl.hp    = static_cast<short>(p_hp);
        cl.exp   = p_exp;
        cl.dmg   = 10 + (p_lv * 3);
        // 로그인 성공: 현재 위치를 이동 응답으로 전달 (UP 방향은 placeholder)
        send_move_response(client_id, GameProtocol::Direction_UP);
    } else {
        // 로그인 실패: EF_FAIL_WRONG_REQ NAK 전송
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    cl.state_lock.lock();
    cl._state = ST_INGAME;
    cl.state_lock.unlock();

    // 주변 플레이어들에게 새 플레이어 알림 + NPC 활성화
    for (auto& other : clients) {
        if (other._id == client_id) continue;
        other.state_lock.lock();
        if (ST_INGAME != other._state) { other.state_lock.unlock(); continue; }
        other.state_lock.unlock();
        if (false == is_near(other._id, client_id)) continue;

        if (is_npc(other._id)) {
            timer_event ev;
            ev.obj_id     = other._id;
            ev.target_id  = client_id;
            ev.start_time = chrono::system_clock::now() + 1s;
            ev.ev         = EVENT_NPC_MOVE;
            timer_queue.push(ev);
            continue;
        }

        other.vl.lock();
        other.viewlist.insert(client_id);
        other.vl.unlock();
        // 주변 플레이어에게 새 플레이어 이동 응답 전송
        send_move_response(other._id, GameProtocol::Direction_UP);
    }

    // 새 플레이어에게 주변 오브젝트 정보 전송
    for (auto& other : clients) {
        if (other._id == client_id) continue;
        other.state_lock.lock();
        if (ST_INGAME != other._state) { other.state_lock.unlock(); continue; }
        other.state_lock.unlock();
        if (false == is_near(other._id, client_id)) continue;

        clients[client_id].vl.lock();
        clients[client_id].viewlist.insert(other._id);
        clients[client_id].vl.unlock();
        // 새 플레이어에게 주변 오브젝트의 이동 응답 전송
        send_move_response(client_id, GameProtocol::Direction_UP);
    }
}

// CS_PlayerMoveRequest (201): 플레이어 이동
void handle_move(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    // NAK 전송 람다: EErrorMsg를 이동 응답(messegeid=에러코드)으로 전달 후 false 반환
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        auto framed = FBProtocol::BuildMoveNak(err);
        send_fb_packet(client_id, framed);
        return false;
    };

    const GameProtocol::CSPlayerMoveRequest* req =
        FBProtocol::ParseCSPlayerMoveRequest(fb_data, fb_size);
    if (!req) {
        cout << "[handle_move] Invalid CSPlayerMoveRequest from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    CLIENT& cl = clients[client_id];
    GameProtocol::Direction dir = req->direction();

    int x = cl.x;
    int y = cl.y;
    switch (static_cast<int>(dir)) {
    case 0: if (y > 0 && obs[y-1][x] == 0) y--; break;              // UP
    case 1: if (y < (WORLD_HEIGHT - 1) && obs[y+1][x] == 0) y++; break; // DOWN
    case 2: if (x > 0 && obs[y][x-1] == 0) x--; break;              // LEFT
    case 3: if (x < (WORLD_WIDTH - 1) && obs[y][x+1] == 0) x++; break;  // RIGHT
    default:
        cout << "[handle_move] Invalid direction from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }
    cl.x = static_cast<short>(x);
    cl.y = static_cast<short>(y);

    // nearlist: 이동 후 시야 내 오브젝트 목록
    unordered_set<int> nearlist;
    for (auto& other : clients) {
        if (other._id == client_id) continue;
        if (ST_INGAME != other.get_state()) continue;
        if (false == is_near(client_id, other.get_id())) continue;
        if (is_npc(other.get_id())) {
            if (!other._is_active) {
                other.set_active(true);
                timer_event ev;
                ev.obj_id     = other.get_id();
                ev.start_time = chrono::system_clock::now() + 1s;
                ev.target_id  = client_id;
                timer_queue.push(ev);
                Activate_Player_Move_Event(other.get_id(), cl.get_id());
            }
        }
        nearlist.insert(other.get_id());
    }

    // 자신에게 이동 결과 전송
    send_move_response(cl._id, dir);

    cl.vl.lock();
    unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    // 새로 시야에 들어온 오브젝트 처리
    for (auto other : nearlist) {
        if (0 == my_vl.count(other)) {
            cl.vl.lock();
            cl.viewlist.insert(other);
            cl.vl.unlock();
            send_move_response(cl._id, dir); // 새 오브젝트에 대한 put 대용

            if (is_npc(other)) {
                timer_event ev;
                ev.obj_id     = other;
                ev.target_id  = client_id;
                ev.start_time = chrono::system_clock::now() + 1s;
                ev.ev         = EVENT_NPC_MOVE;
                timer_queue.push(ev);
                continue;
            }

            clients[other].vl.lock();
            if (0 == clients[other].viewlist.count(cl._id)) {
                clients[other].viewlist.insert(cl._id);
                clients[other].vl.unlock();
                send_move_response(other, dir);
            } else {
                clients[other].vl.unlock();
                send_move_response(other, dir);
            }
        } else {
            if (is_npc(other)) continue;
            clients[other].vl.lock();
            bool in_other_vl = clients[other].viewlist.count(cl._id) > 0;
            if (in_other_vl) {
                clients[other].vl.unlock();
            } else {
                clients[other].viewlist.insert(cl._id);
                clients[other].vl.unlock();
            }
            send_move_response(other, dir);
        }
    }

    // 시야에서 사라진 오브젝트 처리
    for (auto other : my_vl) {
        if (0 == nearlist.count(other)) {
            cl.vl.lock();
            cl.viewlist.erase(other);
            cl.vl.unlock();

            if (is_npc(other)) continue;

            clients[other].vl.lock();
            if (0 != clients[other].viewlist.count(cl._id)) {
                clients[other].viewlist.erase(cl._id);
                clients[other].vl.unlock();
            } else {
                clients[other].vl.unlock();
            }
        }
    }
}

// CS_PlayerAttackRequest (205): 공격
void handle_attack(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    // NAK 전송 람다: EErrorMsg를 공격 응답(target_id=에러코드)으로 전달 후 false 반환
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        auto framed = FBProtocol::BuildAttackNak(err);
        send_fb_packet(client_id, framed);
        return false;
    };

    const GameProtocol::CSPlayerAttackRequest* req =
        FBProtocol::ParseCSPlayerAttackRequest(fb_data, fb_size);
    if (!req) {
        cout << "[handle_attack] Invalid CSPlayerAttackRequest from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    CLIENT& cl = clients[client_id];
    GameProtocol::Direction dir = req->direction();

    cl.vl.lock();
    unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    for (auto& k : my_vl) {
        if (is_attack_range(client_id, k)) {
            clients[client_id].hp -= clients[k].dmg;
            clients[k].hp         -= clients[client_id].dmg;
            // 피격 대상(k) HP 정보를 공격자에게 전송
            send_attack_response(client_id, k, dir,
                                 clients[k].hp, clients[k].exp);
        }
    }
    // 자기 자신 HP 업데이트
    send_attack_response(client_id, client_id, dir,
                         clients[client_id].hp, clients[client_id].exp);
}

// CS_PlayerChattingRequest (301): 채팅
void handle_chatting(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    // NAK 전송 람다: EErrorMsg를 채팅 응답(id=에러코드, message=에러이름)으로 전달 후 false 반환
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        auto framed = FBProtocol::BuildChattingNak(err);
        send_fb_packet(client_id, framed);
        return false;
    };

    const GameProtocol::CSPlayerChattingRequest* req =
        FBProtocol::ParseCSPlayerChattingRequest(fb_data, fb_size);
    if (!req || !req->message()) {
        cout << "[handle_chatting] Invalid CSPlayerChattingRequest from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    const char* message = req->message()->c_str();

    CLIENT& cl = clients[client_id];
    cl.vl.lock();
    unordered_set<int> my_vl{ cl.viewlist };
    cl.vl.unlock();

    // 자신에게 전송
    send_chatting_response(client_id, client_id, message);
    // 시야 내 플레이어들에게 전송
    for (auto other : my_vl) {
        if (is_player(other)) {
            send_chatting_response(other, client_id, message);
        }
    }
}

// CS_RandomTeleportRequest (401): 랜덤 텔레포트 (더미 동접 테스트용)
void handle_random_teleport(int client_id, const uint8_t* fb_data, uint32_t fb_size)
{
    // NAK 전송 람다: EErrorMsg를 이동 응답(messegeid=에러코드)으로 전달 후 false 반환
    auto SendNak = [&](GameProtocol::EErrorMsg err) -> bool {
        auto framed = FBProtocol::BuildMoveNak(err);
        send_fb_packet(client_id, framed);
        return false;
    };

    // 파싱 및 검증
    const GameProtocol::CSRandomTeleportRequest* req =
        FBProtocol::ParseCSRandomTeleportRequest(fb_data, fb_size);
    if (!req) {
        cout << "[handle_random_teleport] Invalid CSRandomTeleportRequest from client " << client_id << endl;
        SendNak(GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
        return;
    }

    CLIENT& cl = clients[client_id];

    // 시야 내 오브젝트들의 viewlist 정리
    cl.vl.lock();
    unordered_set<int> old_vl{ cl.viewlist };
    cl.viewlist.clear();
    cl.vl.unlock();

    for (auto other : old_vl) {
        if (!is_npc(other)) {
            clients[other].vl.lock();
            clients[other].viewlist.erase(client_id);
            clients[other].vl.unlock();
        }
    }

    // 랜덤 위치로 이동
    int new_x, new_y;
    do {
        new_x = rand() % WORLD_WIDTH;
        new_y = rand() % WORLD_HEIGHT;
    } while (obs[new_y][new_x]);

    cl.x = static_cast<short>(new_x);
    cl.y = static_cast<short>(new_y);

    // 새 위치를 이동 응답으로 알림
    send_move_response(client_id, GameProtocol::Direction_UP);

    // 새 위치 주변 오브젝트 처리
    for (auto& other : clients) {
        if (other._id == client_id) continue;
        if (ST_INGAME != other.get_state()) continue;
        if (!is_near(client_id, other.get_id())) continue;

        cl.vl.lock();
        cl.viewlist.insert(other._id);
        cl.vl.unlock();
        send_move_response(client_id, GameProtocol::Direction_UP);

        if (!is_npc(other._id)) {
            clients[other._id].vl.lock();
            clients[other._id].viewlist.insert(client_id);
            clients[other._id].vl.unlock();
            send_move_response(other._id, GameProtocol::Direction_UP);
        }
    }
}

// -----------------------------------------------------------------------
// 전역 핸들러 매니저 및 등록
// -----------------------------------------------------------------------
PacketHandlerManager g_packet_handler;

void RegisterAllHandlers()
{
    g_packet_handler.RegisterHandler(CS_LOGIN_REQUEST,         handle_login);
    g_packet_handler.RegisterHandler(CS_PLAYER_MOVE_REQUEST,   handle_move);
    g_packet_handler.RegisterHandler(CS_PLAYER_ATTACK_REQUEST, handle_attack);
    g_packet_handler.RegisterHandler(CS_PLAYER_CHATTING_REQUEST, handle_chatting);
    g_packet_handler.RegisterHandler(CS_RANDOM_TELEPORT_REQUEST, handle_random_teleport);
}

// -----------------------------------------------------------------------
// Worker thread
//
// 수신 패킷 포맷: [4바이트 messegeid (int32 LE)][FlatBuffers 데이터]
// -----------------------------------------------------------------------
void worker()
{
    for (;;) {
        DWORD num_byte;
        LONG64 iocp_key;
        WSAOVERLAPPED* p_over;
        BOOL ret = GetQueuedCompletionStatus(
            g_h_iocp, &num_byte, (PULONG_PTR)&iocp_key, &p_over, INFINITE);

        int client_id    = static_cast<int>(iocp_key);
        EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(p_over);

        if (FALSE == ret) {
            int err_no = WSAGetLastError();
            cout << "GQCS Error : ";
            error_display(err_no);
            cout << endl;
            Disconnect(client_id);
            if (exp_over->_comp_op == OP_SEND)
                delete exp_over;
            continue;
        }

        switch (exp_over->_comp_op) {
        case OP_RECV: {
            if (num_byte == 0) {
                Disconnect(client_id);
                continue;
            }

            CLIENT& cl = clients[client_id];
            int total = num_byte + cl._prev_size;
            uint8_t* buf = exp_over->_net_buf;

            // 패킷 포맷: [4바이트 messegeid][FlatBuffers 데이터(가변)]
            // FlatBuffers 데이터는 앞 4바이트에 자체 크기를 포함하지 않으므로
            // 다음 패킷 경계를 알기 위해 추가 크기 필드가 필요합니다.
            // 실제 포맷: [4바이트 messegeid][4바이트 fb_size][fb_size바이트 FB 데이터]
            // 최소 헤더 = 8바이트
            while (total >= 8) {
                int32_t  messege_id = FBProtocol::ReadMessegeId(buf);
                uint32_t fb_size    =
                    static_cast<uint32_t>(buf[4])        |
                    (static_cast<uint32_t>(buf[5]) << 8)  |
                    (static_cast<uint32_t>(buf[6]) << 16) |
                    (static_cast<uint32_t>(buf[7]) << 24);
                uint32_t full_size = 4 + 4 + fb_size; // messegeid + fb_size + fb_data

                if (total < static_cast<int>(full_size)) break;

                const uint8_t* fb_data = buf + 8;
                g_packet_handler.Dispatch(client_id, messege_id, fb_data, fb_size);

                total -= static_cast<int>(full_size);
                buf   += full_size;
            }

            if (total > 0) {
                cl._prev_size = total;
                memcpy(exp_over->_net_buf, buf, total);
            } else {
                cl._prev_size = 0;
            }
            cl.do_recv();
            break;
        }
        case OP_SEND: {
            if (num_byte != exp_over->_wsa_buf.len) {
                Disconnect(client_id);
            }
            delete exp_over;
            break;
        }
        case OP_ACCEPT: {
            SOCKET c_socket = *(reinterpret_cast<SOCKET*>(exp_over->_net_buf));
            int new_id = get_new_id();
            if (-1 == new_id) {
                cout << "Maximum user overflow. Accept aborted.\n";
            } else {
                CLIENT& cl = clients[new_id];
                cl.x = rand() % WORLD_WIDTH;
                cl.y = rand() % WORLD_HEIGHT;
                cl._id        = new_id;
                cl._prev_size = 0;
                cl._recv_over._comp_op           = OP_RECV;
                cl._recv_over._wsa_buf.buf        =
                    reinterpret_cast<char*>(cl._recv_over._net_buf);
                cl._recv_over._wsa_buf.len        = sizeof(cl._recv_over._net_buf);
                ZeroMemory(&cl._recv_over._wsa_over, sizeof(cl._recv_over._wsa_over));
                cl._socket = c_socket;

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
            clients[client_id].state_lock.lock();
            if (clients[client_id].get_state() != ST_INGAME) {
                clients[client_id].state_lock.unlock();
                clients[client_id].set_active(false);
                delete exp_over;
                break;
            }
            clients[client_id].state_lock.unlock();

            if (exp_over->_target == -1) {
                delete exp_over;
                break;
            }

            clients[client_id].Lua_Lock.lock();
            lua_State* L = clients[client_id].L;
            lua_getglobal(L, "event_NPC_move");
            lua_pushnumber(L, exp_over->_target);
            int err = lua_pcall(L, 1, 1, 0);
            if (err != 0) {
                cout << "[Lua] event_NPC_move ERR: "
                     << lua_tostring(L, -1) << endl;
                lua_pop(L, 1);
                clients[client_id].Lua_Lock.unlock();
                delete exp_over;
                break;
            }
            bool should_move = lua_toboolean(L, -1);
            lua_pop(L, 1);
            clients[client_id].Lua_Lock.unlock();

            if (should_move) do_npc_move(client_id, exp_over->_target);
            delete exp_over;
            break;
        }
        case OP_PLAYER_MOVE: {
            clients[client_id].Lua_Lock.lock();
            lua_State* L = clients[client_id].L;
            lua_getglobal(L, "event_player_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 0, 0);
            if (error != 0) {
                cout << lua_tostring(L, -1) << endl;
            }
            clients[client_id].Lua_Lock.unlock();
            delete exp_over;
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
    lua_pop(L, 2);
    lua_pushnumber(L, clients[user_id].x);
    return 1;
}

int API_get_y(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    lua_pushnumber(L, clients[user_id].y);
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
        clients[i]._type = rand() % 2 + 2;
        if (clients[i]._type == 2) sprintf_s(clients[i].name, "DOG%d", i);
        if (clients[i]._type == 3) sprintf_s(clients[i].name, "CAT%d", i);

        clients[i].x          = rand() % WORLD_WIDTH;
        clients[i].y          = rand() % WORLD_HEIGHT;
        clients[i]._id        = i;
        clients[i]._state     = ST_INGAME;
        clients[i]._is_active = false;
        clients[i].exp        = 100;
        clients[i].hp         = 100;
        clients[i].maxhp      = 100;
        clients[i].dmg        = 10 + (1 * 3);

        lua_State* L = clients[i].L = luaL_newstate();
        luaL_openlibs(L);
        luaL_loadfile(L, "monster.lua");
        lua_pcall(L, 0, 0, 0);
        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        lua_pushnumber(L, clients[i].x);
        lua_pushnumber(L, clients[i].y);
        lua_pcall(L, 3, 3, 0);
        lua_pop(L, 4);

        lua_register(L, "API_get_x",       API_get_x);
        lua_register(L, "API_get_y",       API_get_y);
        lua_register(L, "API_SendMessage", API_SendMessage);
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
// NPC 이동
// -----------------------------------------------------------------------
void npcmove(int npc_id)
{
    auto& x = clients[npc_id].x;
    auto& y = clients[npc_id].y;
    switch (rand() % 4) {
    case 0: if (y > 0 && (obs[y-1][x] == 0)) y--; break;
    case 1: if (y < (WORLD_HEIGHT - 1) && (obs[y+1][x] == 0)) y++; break;
    case 2: if (x > 0 && (obs[y][x-1] == 0)) x--; break;
    case 3: if (x < (WORLD_WIDTH - 1) && (obs[y][x+1] == 0)) x++; break;
    }
    timer_event ev;
    ev.obj_id     = npc_id;
    ev.target_id  = 0;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev         = EVENT_NPC_MOVE;
    timer_queue.push(ev);
}

void do_npc_move(int npc_id, int target, std::chrono::seconds time)
{
    // 이동 전 시야 내 플레이어
    unordered_set<int> old_viewlist;
    for (auto& obj : clients) {
        if (obj._state != ST_INGAME)       continue;
        if (!is_player(obj.get_id()))      continue;
        if (is_near(npc_id, obj.get_id())) old_viewlist.insert(obj._id);
    }

    // 타겟 방향으로 1칸 이동
    auto& x   = clients[npc_id].x;
    auto& y   = clients[npc_id].y;
    short t_x = clients[target].x;
    short t_y = clients[target].y;

    if      (t_x != x) x += (t_x > x) ? 1 : -1;
    else if (t_y != y) y += (t_y > y) ? 1 : -1;

    // 이동 후 시야 내 플레이어
    unordered_set<int> new_viewlist;
    for (auto& obj : clients) {
        if (obj._state != ST_INGAME)       continue;
        if (!is_player(obj.get_id()))      continue;
        if (is_near(npc_id, obj.get_id())) new_viewlist.insert(obj.get_id());
    }

    // 새로 시야에 들어온 플레이어 → put (이동 응답으로 대체)
    // 계속 시야에 있는 플레이어 → 이동 응답
    for (auto pl : new_viewlist) {
        if (old_viewlist.count(pl) == 0) {
            clients[pl].vl.lock();
            clients[pl].viewlist.insert(npc_id);
            clients[pl].vl.unlock();
        }
        send_move_response(pl, GameProtocol::Direction_UP);
    }

    // 시야에서 사라진 플레이어 처리
    for (auto pl : old_viewlist) {
        if (new_viewlist.count(pl) == 0) {
            clients[pl].vl.lock();
            clients[pl].viewlist.erase(npc_id);
            clients[pl].vl.unlock();
        }
    }

    // 다음 이동 타이머 등록
    clients[npc_id].state_lock.lock();
    if (clients[npc_id].get_state() != ST_INGAME) {
        clients[npc_id].state_lock.unlock();
        return;
    }
    clients[npc_id].state_lock.unlock();

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
    chrono::system_clock::duration dura;
    const chrono::milliseconds waittime = 10ms;
    timer_event temp;
    bool temp_bool = false;
    while (true) {
        if (temp_bool) {
            temp_bool = false;
            EXP_OVER* ex_over = new EXP_OVER;
            ex_over->_comp_op = OP_NPC_MOVE;
            PostQueuedCompletionStatus(
                g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                ex_over->_comp_op = OP_NPC_MOVE;
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
        this_thread::sleep_for(dura);
    }
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main()
{
    Initialize_DB();
    Initialize_NPC();
    Initialize_obstacle();
    RegisterAllHandlers();
    cout << "NPC initialize fin" << endl;
    wcout.imbue(locale("korean"));

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_s_socket,
         reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);

    g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 0, 0);

    SOCKET   c_socket = WSASocket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    char     accept_buf[sizeof(SOCKADDR_IN) * 2 + 32 + 100];
    EXP_OVER accept_ex;
    *(reinterpret_cast<SOCKET*>(&accept_ex._net_buf)) = c_socket;
    ZeroMemory(&accept_ex._wsa_over, sizeof(accept_ex._wsa_over));
    accept_ex._comp_op = OP_ACCEPT;

    AcceptEx(g_s_socket, c_socket, accept_buf, 0,
             sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
             NULL, &accept_ex._wsa_over);
    cout << "Accept Called\n";

    for (int i = 0; i < MAX_USER; ++i)
        clients[i]._id = i;
    cout << "Creating Worker Threads\n";

    vector<thread> worker_threads;
    thread timer_thread{ do_timer };
    for (int i = 0; i < 16; ++i)
        worker_threads.emplace_back(worker);
    for (auto& th : worker_threads)
        th.join();

    timer_thread.join();
    for (auto& cl : clients) {
        if (ST_INGAME == cl._state)
            Disconnect(cl._id);
    }
    closesocket(g_s_socket);
    WSACleanup();
    Disconnect_DB();
}
