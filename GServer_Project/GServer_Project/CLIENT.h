#pragma once
#include "stdafx.h"
#include "Server.h"

class CLIENT {
public:
    char name[MAX_NAME_SIZE];
    int      _id;
    short  x, y;
    short	level;
    short	hp, maxhp;
    short	exp;
    short   dmg;
    unordered_set   <int>  viewlist;
    mutex vl;
    lua_State* L;

    mutex Lua_Lock;
    mutex state_lock;
    STATE _state;
    atomic_bool   _is_active;
    int      _type;   // 1.Player   2.NPC(dog) 3. NPC(cat)   

    EXP_OVER _recv_over;
    SOCKET  _socket;
    int      _prev_size;
    int      last_move_time;
public:
    CLIENT() : _state(ST_FREE), _prev_size(0)
    {
        x = 0;
        y = 0;
    }

    ~CLIENT()
    {
        closesocket(_socket);
    }

    void do_recv()
    {
        DWORD recv_flag = 0;
        ZeroMemory(&_recv_over._wsa_over, sizeof(_recv_over._wsa_over));
        _recv_over._wsa_buf.buf = reinterpret_cast<char*>(_recv_over._net_buf + _prev_size);
        _recv_over._wsa_buf.len = sizeof(_recv_over._net_buf) - _prev_size;
        int ret = WSARecv(_socket, &_recv_over._wsa_buf, 1, 0, &recv_flag, &_recv_over._wsa_over, NULL);
        if (SOCKET_ERROR == ret) {
            int error_num = WSAGetLastError();
            if (ERROR_IO_PENDING != error_num)
                error_display(error_num);
        }
    }

    void do_send(int num_bytes, void* mess)
    {
        EXP_OVER* ex_over = new EXP_OVER(OP_SEND, num_bytes, mess);
        int ret = WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
        if (SOCKET_ERROR == ret) {
            int error_num = WSAGetLastError();
            if (ERROR_IO_PENDING != error_num)
                error_display(error_num);
        }
    }
};