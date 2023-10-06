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

    void do_recv();
    void do_send(int num_bytes, void* mess);
    void set_id(int id);
    void set_exp(short exp);
    void set_hp(short hp);
    void set_maxhp(short maxhp);
    void set_level(short level);
    int get_id();
    STATE get_state();
    atomic_bool set_active(bool active);
};