#pragma once
#include "stdafx.h"

// -----------------------------------------------------------------------
// Grid / Cell 공간 분할 시스템
//
// 목적:
//   do_npc_move의 전체 clients 배열 순회(O(210,000))를
//   인접 셀 탐색(O(~6))으로 교체하여 NPC 이동 연산 비용을 절감한다.
//
// 설계:
//   CELL_SIZE = 16  (RANGE=15 기준, 인접 3×3 셀로 완전 커버)
//   GRID_W    = WORLD_WIDTH  / CELL_SIZE = 125
//   GRID_H    = WORLD_HEIGHT / CELL_SIZE = 125
//
//   각 GridCell은 mutex + 플레이어 ID 집합 + NPC ID 집합 + 인접 셀 캐시를 가진다.
//   near_cells: 초기화 시 3×3 이웃 셀 포인터를 미리 계산해 두어
//               탐색 시마다 범위 계산을 생략한다.
// -----------------------------------------------------------------------

constexpr int CELL_SIZE = 16;
constexpr int GRID_W    = WORLD_WIDTH  / CELL_SIZE;   // 125
constexpr int GRID_H    = WORLD_HEIGHT / CELL_SIZE;   // 125

struct GridCell {
    std::mutex              mtx;
    std::unordered_set<int> players;    // 플레이어 ID (0 ~ MAX_USER-1)
    std::unordered_set<int> npcs;       // NPC ID    (NPC_ID_START ~ NPC_ID_END)
    std::vector<GridCell*>  near_cells; // 3×3 인접 셀 포인터 캐시 (최대 9개)
};

extern GridCell g_grid[GRID_H][GRID_W];

// -----------------------------------------------------------------------
// 셀 인덱스 계산 헬퍼 (좌표 → 셀 인덱스, 경계 클램프 포함)
// -----------------------------------------------------------------------
inline int cell_x(int x) { return std::max(0, std::min(x / CELL_SIZE, GRID_W - 1)); }
inline int cell_y(int y) { return std::max(0, std::min(y / CELL_SIZE, GRID_H - 1)); }

// -----------------------------------------------------------------------
// 초기화 (main 진입 직후, Initialize_NPC 이전에 반드시 호출)
//   모든 셀의 near_cells 캐시를 미리 계산한다.
// -----------------------------------------------------------------------
void grid_initialize();

// -----------------------------------------------------------------------
// 플레이어 셀 등록 / 제거 / 이동
// -----------------------------------------------------------------------
void grid_add_player   (int id, int x,     int y);
void grid_remove_player(int id, int x,     int y);
void grid_move_player  (int id,
                        int old_cx, int old_cy,
                        int new_cx, int new_cy);

// -----------------------------------------------------------------------
// NPC 셀 등록 / 제거 / 이동
// -----------------------------------------------------------------------
void grid_add_npc   (int id, int x,     int y);
void grid_remove_npc(int id, int x,     int y);
void grid_move_npc  (int id,
                     int old_cx, int old_cy,
                     int new_cx, int new_cy);

// -----------------------------------------------------------------------
// 조회
//
// grid_get_near_players
//   (cx, cy) 기준 3×3 셀 내 플레이어 ID 를 out 에 삽입.
//   exclude_id 가 >= 0 이면 해당 ID 는 제외한다.
//
// grid_get_inout_players
//   (old_cx, old_cy) → (new_cx, new_cy) 이동 시
//   새로 시야에 들어온 플레이어 → in_players
//   시야에서 사라진 플레이어    → out_players
//   (exclude_id 는 양쪽 모두 제외)
// -----------------------------------------------------------------------
void grid_get_near_players(int cx, int cy,
                           std::unordered_set<int>& out,
                           int exclude_id = -1);

void grid_get_inout_players(int old_cx, int old_cy,
                            int new_cx, int new_cy,
                            std::unordered_set<int>& in_players,
                            std::unordered_set<int>& out_players,
                            int exclude_id = -1);
