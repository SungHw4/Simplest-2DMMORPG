#pragma once
#include "BaseObject.h"

// -----------------------------------------------------------------------
// NPC : BaseObject
//   Lua AI에 필요한 필드만 추가.
//   플레이어에는 없는 필드: npc_type, L, Lua_Lock
// -----------------------------------------------------------------------
class NPC : public BaseObject
{
public:
    int        npc_type = 0;    // 2 = DOG,  3 = CAT
    lua_State* L        = nullptr;
    std::mutex Lua_Lock;

    NPC() { x = 0; y = 0; }

    ~NPC() override
    {
        if (L)
        {
            lua_close(L);
            L = nullptr;
        }
    }
};
