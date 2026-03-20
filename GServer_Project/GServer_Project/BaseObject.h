#pragma once
#include "stdafx.h"
#include "Server.h"

// -----------------------------------------------------------------------
// BaseObject
//   Player와 NPC의 공통 베이스 클래스.
//   위치(x,y), 스탯(hp,exp,level), 상태(viewlist, _state) 등
//   두 클래스 모두 사용하는 필드만 포함한다.
//
//   Player : BaseObject  →  소켓/IOCP 수신 필드 추가
//   NPC    : BaseObject  →  Lua AI 필드 추가
// -----------------------------------------------------------------------
class BaseObject
{
public:
    int                     _id     = 0;
    char                    name[NAME_LEN] = {};
    std::atomic<short>      x{0}, y{0};
    short                   level   = 0;
    std::atomic<short>      hp{0}, maxhp{0};
    short                   exp     = 0;
    short                   dmg     = 0;

    std::unordered_set<int> viewlist;
    std::mutex              vl;
    std::mutex              state_lock;
    STATE                   _state  = ST_FREE;
    std::atomic_bool        _is_active{false};

    BaseObject() = default;
    virtual ~BaseObject() = default;

    // mutex 멤버로 인해 복사/이동 금지
    BaseObject(const BaseObject&)            = delete;
    BaseObject& operator=(const BaseObject&) = delete;

    STATE get_state()          { return _state; }
    void  set_active(bool act) { _is_active = act; }
    int   get_id()             { return _id; }
};
