#pragma once
#include "Player.h"
#include "NPC.h"
#include "protocol.h"

// -----------------------------------------------------------------------
// GameObjects.h
//   전역 Player / NPC 배열 extern 선언 + ID 기반 접근 헬퍼.
//
//   실제 배열 정의는 ServerMain.cpp 에 있다.
//
//   사용법:
//     GetObject(id)  → BaseObject*  (플레이어/NPC 공통 필드)
//     GetPlayer(id)  → Player*      (소켓/IOCP 전용 필드)
//     GetNPC(id)     → NPC*         (Lua 전용 필드)
// -----------------------------------------------------------------------

extern std::array<Player, MAX_USER> g_players;
extern std::array<NPC,    MAX_NPC>  g_npcs;

// ID → BaseObject* (플레이어/NPC 모두)
inline BaseObject* GetObject(int id)
{
    if (id < MAX_USER) return &g_players[id];
    return &g_npcs[id - MAX_USER];
}

// ID → Player* (반드시 is_player(id) == true 인 경우에만 호출)
inline Player* GetPlayer(int id)
{
    return &g_players[id];
}

// ID → NPC* (반드시 is_npc(id) == true 인 경우에만 호출)
inline NPC* GetNPC(int id)
{
    return &g_npcs[id - MAX_USER];
}
