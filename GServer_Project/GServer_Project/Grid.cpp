#include "stdafx.h"
#include "Grid.h"

// -----------------------------------------------------------------------
// 전역 그리드 배열 정의
// -----------------------------------------------------------------------
GridCell g_grid[GRID_H][GRID_W];

// -----------------------------------------------------------------------
// grid_initialize
//   모든 셀의 near_cells 캐시를 미리 계산한다.
//   (main 시작 시 Initialize_NPC 이전에 호출)
// -----------------------------------------------------------------------
void grid_initialize()
{
    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            auto& cell = g_grid[gy][gx];
            cell.near_cells.clear();
            // 3×3 이웃 (자기 자신 포함, 경계 밖 제외)
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int ny = gy + dy;
                    int nx = gx + dx;
                    if (ny >= 0 && ny < GRID_H && nx >= 0 && nx < GRID_W)
                        cell.near_cells.push_back(&g_grid[ny][nx]);
                }
            }
        }
    }
}

// -----------------------------------------------------------------------
// 플레이어 등록 / 제거
// -----------------------------------------------------------------------
void grid_add_player(int id, int x, int y)
{
    int cx = cell_x(x), cy = cell_y(y);
    std::lock_guard<std::mutex> lock(g_grid[cy][cx].mtx);
    g_grid[cy][cx].players.insert(id);
}

void grid_remove_player(int id, int x, int y)
{
    int cx = cell_x(x), cy = cell_y(y);
    std::lock_guard<std::mutex> lock(g_grid[cy][cx].mtx);
    g_grid[cy][cx].players.erase(id);
}

// -----------------------------------------------------------------------
// grid_move_player
//   셀이 바뀌는 경우에만 erase/insert 수행.
//   두 셀을 동시에 잠글 때 포인터 주소 오름차순으로 획득하여 데드락을 방지한다.
// -----------------------------------------------------------------------
void grid_move_player(int id,
                      int old_cx, int old_cy,
                      int new_cx, int new_cy)
{
    if (old_cx == new_cx && old_cy == new_cy) return;

    GridCell* old_cell = &g_grid[old_cy][old_cx];
    GridCell* new_cell = &g_grid[new_cy][new_cx];

    if (old_cell < new_cell) {
        std::lock_guard<std::mutex> l1(old_cell->mtx);
        std::lock_guard<std::mutex> l2(new_cell->mtx);
        old_cell->players.erase(id);
        new_cell->players.insert(id);
    } else {
        std::lock_guard<std::mutex> l1(new_cell->mtx);
        std::lock_guard<std::mutex> l2(old_cell->mtx);
        old_cell->players.erase(id);
        new_cell->players.insert(id);
    }
}

// -----------------------------------------------------------------------
// NPC 등록 / 제거
// -----------------------------------------------------------------------
void grid_add_npc(int id, int x, int y)
{
    int cx = cell_x(x), cy = cell_y(y);
    std::lock_guard<std::mutex> lock(g_grid[cy][cx].mtx);
    g_grid[cy][cx].npcs.insert(id);
}

void grid_remove_npc(int id, int x, int y)
{
    int cx = cell_x(x), cy = cell_y(y);
    std::lock_guard<std::mutex> lock(g_grid[cy][cx].mtx);
    g_grid[cy][cx].npcs.erase(id);
}

// -----------------------------------------------------------------------
// grid_move_npc
//   grid_move_player 와 동일한 데드락 방지 패턴 적용.
// -----------------------------------------------------------------------
void grid_move_npc(int id,
                   int old_cx, int old_cy,
                   int new_cx, int new_cy)
{
    if (old_cx == new_cx && old_cy == new_cy) return;

    GridCell* old_cell = &g_grid[old_cy][old_cx];
    GridCell* new_cell = &g_grid[new_cy][new_cx];

    if (old_cell < new_cell) {
        std::lock_guard<std::mutex> l1(old_cell->mtx);
        std::lock_guard<std::mutex> l2(new_cell->mtx);
        old_cell->npcs.erase(id);
        new_cell->npcs.insert(id);
    } else {
        std::lock_guard<std::mutex> l1(new_cell->mtx);
        std::lock_guard<std::mutex> l2(old_cell->mtx);
        old_cell->npcs.erase(id);
        new_cell->npcs.insert(id);
    }
}

// -----------------------------------------------------------------------
// grid_get_near_players
//   (cx, cy) 기준 3×3 셀의 near_cells 캐시를 순회하여
//   플레이어 ID 를 out 에 수집한다.
//   각 셀 잠금은 수집 중에만 유지하고 즉시 해제한다.
// -----------------------------------------------------------------------
void grid_get_near_players(int cx, int cy,
                           std::unordered_set<int>& out,
                           int exclude_id)
{
    for (GridCell* cell : g_grid[cy][cx].near_cells) {
        std::lock_guard<std::mutex> lock(cell->mtx);
        for (int id : cell->players) {
            if (id != exclude_id)
                out.insert(id);
        }
    }
}

// -----------------------------------------------------------------------
// grid_get_inout_players
//   (old_cx, old_cy) → (new_cx, new_cy) 이동 시
//   셀 집합의 차집합으로 새로 보이는/사라지는 플레이어를 계산한다.
//
//   in_players  : 새 3×3 셀에만 있는 셀의 플레이어 (새로 시야 진입)
//   out_players : 이전 3×3 셀에만 있는 셀의 플레이어 (시야에서 이탈)
// -----------------------------------------------------------------------
void grid_get_inout_players(int old_cx, int old_cy,
                            int new_cx, int new_cy,
                            std::unordered_set<int>& in_players,
                            std::unordered_set<int>& out_players,
                            int exclude_id)
{
    // 이전/새 3×3 셀 집합 구성
    std::unordered_set<GridCell*> old_cells;
    for (GridCell* cell : g_grid[old_cy][old_cx].near_cells)
        old_cells.insert(cell);

    std::unordered_set<GridCell*> new_cells;
    for (GridCell* cell : g_grid[new_cy][new_cx].near_cells)
        new_cells.insert(cell);

    // 새로 진입한 셀 → in_players
    for (GridCell* cell : new_cells) {
        if (old_cells.count(cell) == 0) {
            std::lock_guard<std::mutex> lock(cell->mtx);
            for (int id : cell->players) {
                if (id != exclude_id)
                    in_players.insert(id);
            }
        }
    }

    // 이탈한 셀 → out_players
    for (GridCell* cell : old_cells) {
        if (new_cells.count(cell) == 0) {
            std::lock_guard<std::mutex> lock(cell->mtx);
            for (int id : cell->players) {
                if (id != exclude_id)
                    out_players.insert(id);
            }
        }
    }
}
