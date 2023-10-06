#pragma once

#include "stdafx.h"
#include "DataBase.h"

HANDLE g_h_iocp;
SOCKET g_s_socket;
mutex LuaLock;

BOOL obs[WORLD_HEIGHT][WORLD_WIDTH];

//BOOL LoadDB(string t);
//void UpdatePlayerOnDB(int c_id);

extern SQLINTEGER p_id;
extern SQLINTEGER p_x;
extern SQLINTEGER p_y;
extern SQLINTEGER p_hp;
extern SQLINTEGER p_maxhp;
extern SQLINTEGER p_exp;
extern SQLINTEGER p_lv;

concurrency::concurrent_priority_queue <timer_event> timer_queue;

//void error_display(int err_no);
//void do_npc_move(int npc_id);
void do_npc_move(int npc_id, int target ,std::chrono::seconds time = 1s);
void npcmove(int npc_id);



array <CLIENT, MAX_USER + MAX_NPC> clients;


bool is_near(int a, int b)
{
    if (RANGE < abs(clients[a].x - clients[b].x)) return false;
    if (RANGE < abs(clients[a].y - clients[b].y)) return false;
    return true;
}
bool is_near_ob(int a, int b)
{
    //if (RANGE < abs(obstacle[a].x - clients[b].x)) return false;
    //if (RANGE < abs(obstacle[a].y - clients[b].y)) return false;
    return true;
}
bool is_attack_range(int a, int b)
{
    if (2 < abs(clients[a].x - clients[b].x)) return false;
    if (2 < abs(clients[a].y - clients[b].y)) return false;
    return true;
}

bool is_attacth(int a, int b)
{
    if (3 < abs(clients[a].x - clients[b].x)) return false;
    if (3 < abs(clients[a].y - clients[b].y)) return false;
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
    static int g_id = 0;

    for (int i = 0; i < MAX_USER; ++i) {
        clients[i].state_lock.lock();
        if (ST_FREE == clients[i]._state) {
            clients[i]._state = ST_ACCEPT;
            clients[i].state_lock.unlock();
            return i;
        }
        clients[i].state_lock.unlock();
    }
    cout << "Maximum Number of Clients Overflow!!\n";
    return -1;
}

void send_login_ok_packet(int c_id)
{
    sc_packet_login_ok packet;
    packet.id = c_id;
    packet.type = SC_PACKET_LOGIN_OK;
    packet.size = sizeof(packet);
    packet.x = clients[c_id].x;
    packet.y = clients[c_id].y;
    packet.exp = clients[c_id].exp;
    packet.hp = clients[c_id].hp;
    packet.maxhp = clients[c_id].maxhp;
    packet.level = clients[c_id].level;
    //memcpy(packet.obs, obs, sizeof(packet.obs));
    //packet.type = clients[c_id]._type;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_login_no_packet(int c_id)
{

    sc_packet_login_ok packet;
    packet.id = c_id;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_LOGIN_FAIL;
    packet.x = clients[c_id].x;
    packet.y = clients[c_id].y;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_attack_packet(int c_id)
{
    clients[c_id].hp -= 10;
    sc_packet_attack packet;
    packet.hp = clients[c_id].hp;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_ATTACK;
    packet.x = clients[c_id].x;
    packet.y = clients[c_id].y;
    clients[c_id].do_send(sizeof(packet), &packet);
    //clients[c_id].viewlist;
}

void send_move_packet(int c_id, int mover)
{
    sc_packet_move packet;
    packet.id = mover;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_MOVE;
    packet.x = clients[mover].x;
    packet.y = clients[mover].y;
    packet.hp = clients[mover].hp;
    packet.move_time = clients[mover].last_move_time;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_remove_object(int c_id, int victim)
{
    sc_packet_remove_object packet;
    packet.id = victim;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_REMOVE_OBJECT;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_put_object(int c_id, int target)
{  
    sc_packet_put_object packet;
    packet.id = target;
    cout << clients[target].hp << endl;
    packet.hp = clients[target].hp;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_PUT_OBJECT;
    packet.x = clients[target].x;
    packet.y = clients[target].y;
    strcpy_s(packet.name, clients[target].name);
    packet.object_type = 0;
    clients[c_id].do_send(sizeof(packet), &packet);
}
void send_obstacle_object(int id, int target_id)
{
    sc_packet_obstacle packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_OBSTACLE;
    packet.id = target_id;
   // packet.x = obstacle[id].x;
   // packet.y = obstacle[id].y;
  //  obstacle[id].do_send(sizeof(packet), &packet);
   
}

void send_chat_packet(int user_id, int my_id, char* mess)
{
    sc_packet_chat packet;
    packet.id = my_id;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_CHAT;
    strcpy_s(packet.message, mess);
    clients[user_id].do_send(sizeof(packet), &packet);
}

void Disconnect(int c_id)
{
    CLIENT& cl = clients[c_id];
    //UpdatePlayerOnDB(c_id);
    UpdatePlayerOnDB(c_id, clients[c_id]);
    cl.vl.lock();
    unordered_set <int> my_vl = cl.viewlist;
    cl.vl.unlock();
    for (auto& other : my_vl) {
        CLIENT& target = clients[other];
        if (true == is_npc(target._id)) break;
        if (ST_INGAME != target._state)
            continue;
        target.vl.lock();
        if (0 != target.viewlist.count(c_id)) {
            target.viewlist.erase(c_id);
            target.vl.unlock();
            send_remove_object(other, c_id);
        }
        else target.vl.unlock();
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
    exp_over->_target = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}
void Activate_NPC_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = new EXP_OVER;
    exp_over->_comp_op = OP_NPC_MOVE;
    exp_over->_target = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}
void process_packet(int client_id, unsigned char* p)
{
    unsigned char packet_type = p[1];
    CLIENT& cl = clients[client_id];

    switch (packet_type) {
    case CS_PACKET_LOGIN: {
        cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
        strcpy_s(cl.name, packet->name);
        if (Load_DB(cl.name)) {
            cl.x = p_x;
            cl.y = p_y;
            cl.level = p_lv;
            cl.maxhp = p_maxhp;
            cl.hp = p_hp;
            cl.exp = p_exp;
            cl.dmg = 10 + (p_lv*3);
            send_login_ok_packet(client_id);
        }
        else
        {
            send_login_no_packet(client_id);
        }
        CLIENT& cl = clients[client_id];
        cl.state_lock.lock();
        cl._state = ST_INGAME;
        cl.state_lock.unlock();

        // ���� ������ �÷��̾��� ������ ���� �÷��̾�� ������
        for (auto& other : clients) {
            if (other._id == client_id) continue;

            other.state_lock.lock();
            if (ST_INGAME != other._state) {
                other.state_lock.unlock();
                continue;
            }
            other.state_lock.unlock();

            if (false == is_near(other._id, client_id))
                continue;

            if (true == is_npc(other._id)) {
                timer_event ev;
                ev.obj_id = other._id;
                ev.target_id = client_id;
                ev.start_time = chrono::system_clock::now() + 1s;
                ev.ev = EVENT_NPC_MOVE;
                timer_queue.push(ev);
                continue;
            }

            other.vl.lock();
            other.viewlist.insert(client_id);
            other.vl.unlock();
            sc_packet_put_object packet;
            packet.id = client_id;
            strcpy_s(packet.name, cl.name);
            packet.object_type = cl._type;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = cl.x;
            packet.hp = cl.hp;
            packet.y = cl.y;
            other.do_send(sizeof(packet), &packet);
        }

        // ���� ������ �÷��̾�� ���� ��ü ������ ������

        for (auto& other : clients) {
            if (other._id == client_id) continue;
            other.state_lock.lock();
            if (ST_INGAME != other._state) {
                other.state_lock.unlock();
                continue;
            }
            other.state_lock.unlock();

            if (false == is_near(other._id, client_id))
                continue;

            clients[client_id].vl.lock();
            clients[client_id].viewlist.insert(other._id);
            clients[client_id].vl.unlock();

            sc_packet_put_object packet;
            packet.id = other._id;
            strcpy_s(packet.name, other.name);
            packet.object_type = other._type;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = other.x;
            packet.hp = other.hp;
            packet.y = other.y;
            cl.do_send(sizeof(packet), &packet);
        }
        break;
    }
    case CS_PACKET_ATTACK: {
        cout << "1" << endl;
        cl.vl.lock();
        unordered_set <int> my_vl{ cl.viewlist };
        cl.vl.unlock();
        for (auto& k : my_vl)
            cout << k << endl;
        
        for (auto& k : my_vl) {
            //cout << endl<< k << endl << endl;
            if (is_attack_range(client_id, k))
            {
                cout << "123" << endl;
                sc_packet_attack* packet = reinterpret_cast<sc_packet_attack*>(p);
                clients[client_id].hp -= clients[k].dmg;
                clients[k].hp -= clients[client_id].dmg;
                packet->id = k;
                packet->size = sizeof(packet);
                packet->hp = clients[k].hp;
                packet->x = clients[k].x;
                packet->y = clients[k].y;
                packet->exp = clients[k].y;
                packet->type = SC_PACKET_ATTACK;
                clients[client_id].do_send(sizeof(packet), &packet);
            }
        };
        sc_packet_attack* packet = reinterpret_cast<sc_packet_attack*>(p);
        cout << clients[client_id].hp << endl;
        packet->id = client_id;
        packet->size = sizeof(packet);
        packet->hp = clients[client_id].hp;
        packet->x = clients[client_id].x;
        packet->y = clients[client_id].y;
        packet->exp = clients[client_id].y;
        packet->type = SC_PACKET_ATTACK;
        clients[client_id].do_send(sizeof(packet), &packet);
        break;
    }
                         
    case CS_PACKET_MOVE: {
        cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
        cl.last_move_time = packet->move_time;
        int x = cl.x;
        int y = cl.y;
        switch (packet->direction) {
        case 0: if (y > 0 && obs[y-1][x] == 0) y--; break;
        case 1: if (y < (WORLD_HEIGHT - 1) && obs[y+1][x] == 0) y++; break;
        case 2: if (x > 0 && obs[y][x-1] == 0) x--; break;
        case 3: if (x < (WORLD_WIDTH - 1) && obs[y][x+1] == 0) x++; break;
        default:
            cout << "Invalid move in client " << client_id << endl;
            exit(-1);
        }
        cl.x = x;
        cl.y = y;

        unordered_set <int> nearlist;
            for (auto& other : clients) {
                if (other._id == client_id)
                    continue;
                if (ST_INGAME != other.get_state())
                    continue;
                if (false == is_near(client_id, other.get_id()))
                    continue;
                if (true == is_npc(other.get_id())) {
                    //Activate_Player_Move_Event(other._id, cl._id);
                    if(other._is_active == false)
                    {
                        other.set_active(true);
                        timer_event ev;
                        ev.obj_id = other.get_id();
                        ev.start_time = chrono::system_clock::now() + 1s;
                        //ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        Activate_Player_Move_Event(other.get_id(), cl.get_id());
                    }
                }
                nearlist.insert(other.get_id());
            }

        send_move_packet(cl._id, cl._id);

        cl.vl.lock();
        unordered_set <int> my_vl{ cl.viewlist };
        cl.vl.unlock();

        //새로 시야에 들어온 플레이어 처리
        for (auto other : nearlist) {
            if (0 == my_vl.count(other)) {
                cl.vl.lock();
                cl.viewlist.insert(other);
                cl.vl.unlock();
                send_put_object(cl._id, other);

                if (true == is_npc(other)) {
                    timer_event ev;
                    ev.obj_id = other;
                    ev.target_id = client_id;
                    ev.start_time = chrono::system_clock::now() + 1s;
                    ev.ev = EVENT_NPC_MOVE;
                    timer_queue.push(ev);
                    continue;
                }
                //if( true == is_npc(other)) break;

                clients[other].vl.lock();
                if (0 == clients[other].viewlist.count(cl._id)) {
                    clients[other].viewlist.insert(cl._id);
                    clients[other].vl.unlock();
                    send_put_object(other, cl._id);
                }
                else {
                    clients[other].vl.unlock();
                    send_move_packet(other, cl._id);
                }
            }
            // 계속 시야에 들어온 플레이어 처리
            else {
                if (true == is_npc(other)) break; //원래있던거는 어차피 npc_move에서 처리함

                clients[other].vl.lock();
                if (0 != clients[other].viewlist.count(cl._id)) {
                    clients[other].vl.unlock();
                    send_move_packet(other, cl._id);
                }
                else {
                    clients[other].viewlist.insert(cl._id);
                    clients[other].vl.unlock();
                    send_put_object(other, cl._id);
                }
            }
        }
        // 시야에서 사라진 플레이어 처리
        for (auto other : my_vl) {
            if (0 == nearlist.count(other)) {
                cl.vl.lock();
                cl.viewlist.erase(other);
                cl.vl.unlock();
                send_remove_object(cl._id, other);

                if (true == is_npc(other)) break;

                clients[other].vl.lock();
                if (0 != clients[other].viewlist.count(cl._id)) {
                    clients[other].viewlist.erase(cl._id);
                    clients[other].vl.unlock();
                    send_remove_object(other, cl._id);
                }
                else clients[other].vl.unlock();
            }
        }
    }
    }
}

void worker()
{
    for (;;) {
        DWORD num_byte;
        LONG64 iocp_key;
        WSAOVERLAPPED* p_over;
        BOOL ret = GetQueuedCompletionStatus(g_h_iocp, &num_byte, (PULONG_PTR)&iocp_key, &p_over, INFINITE);
        //cout << "GQCS returned.\n";
        int client_id = static_cast<int>(iocp_key);
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
            int remain_data = num_byte + cl._prev_size;
            unsigned char* packet_start = exp_over->_net_buf;
            int packet_size = packet_start[0];

            while (packet_size <= remain_data) {
                process_packet(client_id, packet_start);
                remain_data -= packet_size;
                packet_start += packet_size;
                if (remain_data > 0) packet_size = packet_start[0];
                else break;
            }

            if (0 < remain_data) {
                cl._prev_size = remain_data;
                memcpy(&exp_over->_net_buf, packet_start, remain_data);
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
           // cout << "Accept Completed.\n";
            SOCKET c_socket = *(reinterpret_cast<SOCKET*>(exp_over->_net_buf));
            int new_id = get_new_id();
            if (-1 == new_id) {
                cout << "Maxmum user overflow. Accept aborted.\n";
            }
            else {
                CLIENT& cl = clients[new_id];
                cl.x = rand() % WORLD_WIDTH;
                cl.y = rand() % WORLD_HEIGHT;
                cl._id = new_id;
                cl._prev_size = 0;
                cl._recv_over._comp_op = OP_RECV;
                cl._recv_over._wsa_buf.buf = reinterpret_cast<char*>(cl._recv_over._net_buf);
                cl._recv_over._wsa_buf.len = sizeof(cl._recv_over._net_buf);
                ZeroMemory(&cl._recv_over._wsa_over, sizeof(cl._recv_over._wsa_over));
                cl._socket = c_socket;

                CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_h_iocp, new_id, 0);
                cl.do_recv();
            }

            ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
            c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
            *(reinterpret_cast<SOCKET*>(exp_over->_net_buf)) = c_socket;
            AcceptEx(g_s_socket, c_socket, exp_over->_net_buf + 8, 0, sizeof(SOCKADDR_IN) + 16,
                sizeof(SOCKADDR_IN) + 16, NULL, &exp_over->_wsa_over);
        }
                      break;
        case OP_NPC_MOVE: {
                clients[client_id].state_lock.lock();
                if(clients[client_id].get_state() != ST_INGAME)
                {
                    clients[client_id].state_lock.unlock();
                    clients[client_id].set_active(false);
                    delete exp_over;
                    break;
                }
                clients[client_id].state_lock.unlock();
                if(exp_over->_target == -1)
                {
                    delete exp_over;
                    break;
                }

                clients[client_id].Lua_Lock.lock();
                lua_State* L = clients[client_id].L;
                lua_getglobal(L,"event_NPC_move");
                int err = lua_pcall(L,1,1,0);
                if(err != 0) cout << "event_NPC_move ERR" << endl;
                bool m = lua_toboolean(L,-1);
                lua_pop(L,2);
                clients[client_id].Lua_Lock.unlock();
                if(m) do_npc_move(client_id,exp_over->_target);
                delete exp_over;
                break;
        }
        case OP_PLAYER_MOVE: {
            //clients[client_id].Lua_Lock.lock();
            lua_State* L = clients[client_id].L;
            lua_getglobal(L, "event_player_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 0, 0);
            if (error != 0) {
                cout << lua_tostring(L, -1) << endl;
            }
            //clients[client_id].Lua_Lock.unlock();
            delete exp_over;
            break;
        }
        }
    }
}

int API_SendMessage(lua_State* L)
{
    int my_id = (int)lua_tointeger(L, -3);
    int user_id = (int)lua_tointeger(L, -2);
    char* mess = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);

    send_chat_packet(user_id, my_id, mess);
    return 0;
}

int API_get_x(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int x = clients[user_id].x;
    lua_pushnumber(L, x);
    return 1;
}

int API_get_y(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int y = clients[user_id].y;
    lua_pushnumber(L, y);
    return 1;
}
int API_Touch_Message(lua_State* L)
{
    int my_id = (int)lua_tointeger(L, -3);
    int user_id = (int)lua_tointeger(L, -2);
    char* mess = (char*)lua_tostring(L, -1);

    lua_pop(L, 4);
    this_thread::sleep_for(1s);
    for (int i = 1; i < 4; ++i) {
        timer_event ev;
        ev.obj_id = my_id;
        ev.target_id = user_id;
        ev.start_time = chrono::system_clock::now() + chrono::seconds(i);
        ev.ev = EVENT_NPC_MOVE;
        timer_queue.push(ev);
    }
    //do_npc_move(my_id, 1s);
    //do_npc_move(my_id, 2s);
    //do_npc_move(my_id, 3s);

    this_thread::sleep_for(3s);
    send_chat_packet(user_id, my_id, mess);
    return 0;
}

void Initialize_NPC()
{
    for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
        clients[i]._type = rand() % 2 + 2;
        if (clients[i]._type == 2) 
        sprintf_s(clients[i].name, "DOG%d", i);
        if (clients[i]._type == 3)
            sprintf_s(clients[i].name, "CAT%d", i);

        clients[i].x = rand() % WORLD_WIDTH;
        clients[i].y = rand() % WORLD_HEIGHT;
        clients[i]._id = i;
        clients[i]._state = ST_INGAME;
        clients[i]._is_active = false;
        clients[i].exp = 100;
        clients[i].hp = 100;
        clients[i].maxhp = 100;
        clients[i].dmg = 10 + (1 * 3);
        lua_State* L = clients[i].L = luaL_newstate();
        luaL_openlibs(L);
        int error = luaL_loadfile(L, "monster.lua") ||
            lua_pcall(L, 0, 0, 0);
        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        lua_pushnumber(L,clients[i].x);
        lua_pushnumber(L,clients[i].y);
        lua_pcall(L, 3, 3, 0);
        lua_pop(L, 4);// eliminate set_uid from stack after call

        //lua_register(L, "API_SendMessage", API_SendMessage);
        //lua_register(L, "API_Touch_Massage", API_Touch_Message);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);
    }
}
void Initialize_obstacle()
{
   /* for (int i = 0; i < 125'000; ++i)
    {
        
        obstacle[i]._id = i;
        obstacle[i].x = rand() % WORLD_HEIGHT; 
        obstacle[i].y = rand() % WORLD_WIDTH;
    }*/
    srand(2);
    for (int i = 0; i < WORLD_HEIGHT; ++i)
    {
        for (int j = 0; j < WORLD_WIDTH; ++j)
        {
            
            obs[i][j] = rand() % 4;
            if (obs[i][j]) obs[i][j] = 0;
            else obs[i][j] = 1;
        }
    }
}
void npcmove(int npc_id)
{
    auto& x = clients[npc_id].x;
    auto& y = clients[npc_id].y;
    switch (rand() % 4) {
    case 0: if (y > 0 && (obs[y-1][x] == 0)) y--; break;
    case 1: if (y < (WORLD_HEIGHT - 1) && (obs[y+1][x] == 0)) y++; break;
    case 2: if (x > 0 && (obs[y][x-1] == 0)) x--; break;
    case 3: if (x < (WORLD_WIDTH - 1) && (obs[y][x + 1] == 0)) x++; break;
    }

    timer_event ev;
    ev.obj_id = npc_id;
    ev.target_id = 0;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev = EVENT_NPC_MOVE;
    timer_queue.push(ev);
}
void do_npc_move(int npc_id, int target, std::chrono::seconds time)
{
    //int target;
    unordered_set <int> old_viewlist;
    unordered_set <int> new_viewlist;

    for (auto& obj : clients) {
        if (obj._state != ST_INGAME)
            continue;
        if (false == is_player(obj.get_id()))
            continue;
        if (true == is_near(npc_id, obj.get_id()))
            old_viewlist.insert(obj._id);
    }
    auto& x = clients[npc_id].x;
    auto& y = clients[npc_id].y;
    auto& t_x = clients[target].x;
    auto& t_y = clients[target].y;
    
    if (t_x != x) {
        if (t_x > x) x++;
        else x--;
    }
    else if(t_y != y){
        if (t_y > y) y++;
        else y--;
    }
    clients[npc_id].x = x;
    clients[npc_id].y = y;

    for(auto& obj : clients)
    {
        if(obj.get_state() != ST_INGAME) continue;
        if (true == is_npc(obj.get_id())) break;
        if(true == is_near(npc_id ,obj.get_id()))
        {
            new_viewlist.insert(obj.get_id());
        }
    }
    int temp = 0;

    for(auto p1 : new_viewlist)
    {
        if( 0 == old_viewlist.count(p1))
        {
            clients[p1].vl.lock();
            clients[p1].viewlist.insert(npc_id);
            clients[p1].vl.unlock();
            send_put_object(p1,npc_id);
            //reinterpret_cast<CLIENT*>(clients[p1])->vl.lock();
            //reinterpret_cast<CLIENT*>(clients[p1])->viewlist.insert(npc_id);
            //reinterpret_cast<CLIENT*>(clients[p1])->vl.unlock();
            //send_put_object(reinterpret_cast<CLIENT*>(clients[p1]), clients[npc_id]);
        } else
        {
            send_move_packet(p1,npc_id);
        }
    }
    
   /* clients[npc_id].Lua_Lock.lock();
    lua_State* L = clients[npc_id].L;
    
    lua_getglobal(L, "Random_Move");
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    int error = lua_pcall(L, 2, 2, 0);
    if (error != 0) {
        cout << lua_tostring(L, -1) << endl;
    }
    x = lua_tonumber(L, -2);
    y = lua_tonumber(L, -1);
    clients[npc_id].Lua_Lock.unlock();*/

   
    for (auto& obj : clients) {
        if (obj._state != ST_INGAME)
            continue;
        if (false == is_player(obj.get_id()))
            continue;
        if (true == is_near(npc_id, obj.get_id()))
            new_viewlist.insert(obj.get_id());
    }
    // 새로 시야에 들어온 플레이어
    int temp_client_id = 0;
    for (auto pl : new_viewlist) {
        if (0 == old_viewlist.count(pl)) {
            clients[pl].vl.lock();
            clients[pl].viewlist.insert(npc_id);
            clients[pl].vl.unlock();
            send_put_object(pl, npc_id);
        }
        else {
            send_move_packet(pl, npc_id);
        }
        temp_client_id = pl;
    }
    // 시야에서 사라지는 경우
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            clients[pl].vl.lock();
            clients[pl].viewlist.erase(npc_id);
            clients[pl].vl.unlock();
            send_remove_object(pl, npc_id);
        }
    }
    clients[npc_id].state_lock.lock();
    if(clients[npc_id].get_state() != ST_INGAME)
    {
        clients[npc_id].state_lock.unlock();
        return;
    }
    clients[npc_id].state_lock.unlock();
    
    timer_event ev;
    ev.obj_id = npc_id;
    //ev.target_id = temp_client_id;
    ev.target_id = target;
    ev.start_time = chrono::system_clock::now() + time;
    ev.ev = EVENT_NPC_MOVE;
    timer_queue.push(ev);
}
void do_timer() {

    chrono::system_clock::duration dura;
    const chrono::milliseconds waittime = 10ms;
    timer_event temp;
    bool temp_bool = false;
    while (true) {
        if (temp_bool) {
            temp_bool = false;
            EXP_OVER* ex_over = new EXP_OVER;
            ex_over->_comp_op = OP_NPC_MOVE;
            PostQueuedCompletionStatus(g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);
            
            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                ex_over->_comp_op = OP_NPC_MOVE;
                PostQueuedCompletionStatus(g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
            }
            else if (dura <= waittime) {
                temp = ev;
                temp_bool = true;
                break;
            }
            else {
                timer_queue.push(ev);
                break;
            }
        }
        this_thread::sleep_for(dura);

    }
}


int main()
{
    //LoadDB("Youngin");
    Initialize_DB();
    Initialize_NPC();
    Initialize_obstacle();
    cout << "NPC initialize fin" << endl;
    wcout.imbue(locale("korean"));
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);

    g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 0, 0);

    SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    char   accept_buf[sizeof(SOCKADDR_IN) * 2 + 32 + 100];
    EXP_OVER   accept_ex;
    *(reinterpret_cast<SOCKET*>(&accept_ex._net_buf)) = c_socket;
    ZeroMemory(&accept_ex._wsa_over, sizeof(accept_ex._wsa_over));
    accept_ex._comp_op = OP_ACCEPT;

    AcceptEx(g_s_socket, c_socket, accept_buf, 0, sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16, NULL, &accept_ex._wsa_over);
    cout << "Accept Called\n";

    for (int i = 0; i < MAX_USER; ++i)
    {
        clients[i]._id = i;
    }
    cout << "Creating Worker Threads\n";

    

    vector <thread> worker_threads;
    //thread ai_thread{ do_ai };
    thread timer_thread{ do_timer };
    for (int i = 0; i < 16; ++i)
        worker_threads.emplace_back(worker);
    for (auto& th : worker_threads)
        th.join();

    //ai_thread.join();
    timer_thread.join();
    for (auto& cl : clients) {
        if (ST_INGAME == cl._state)
            Disconnect(cl._id);
    }
    closesocket(g_s_socket);
    WSACleanup();
    Disconnect_DB();
}
