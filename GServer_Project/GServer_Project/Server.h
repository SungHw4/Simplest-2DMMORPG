#pragma once
#include "stdafx.h"
#include <unordered_map>

void error_display(int err_no);

// 패킷 핸들러 함수 포인터 타입
// void handler(int client_id, unsigned char* packet_buf)
using PacketHandler = std::function<void(int, unsigned char*)>;

// 패킷 타입별 핸들러를 등록하고 디스패치하는 매니저
class PacketHandlerManager {
public:
    // 패킷 타입에 핸들러 함수를 등록
    void RegisterHandler(unsigned char packet_type, PacketHandler handler)
    {
        handlers_[packet_type] = handler;
    }

    // 패킷 타입을 보고 등록된 핸들러를 호출
    // [FlatBuffers 포맷] packet = [4바이트 크기][FlatBuffers 데이터]
    // packet_type을 별도로 전달받아 디스패치
    void Dispatch(int client_id, uint8_t packet_type, unsigned char* packet) const
    {
        auto it = handlers_.find(packet_type);
        if (it != handlers_.end()) {
            it->second(client_id, packet);
        } else {
            cout << "[PacketHandlerManager] Unknown packet type: "
                 << static_cast<int>(packet_type) << endl;
        }
    }

    // Legacy overload (타입이 packet[1]에 있는 경우)
    void Dispatch(int client_id, unsigned char* packet) const
    {
        unsigned char packet_type = packet[1];
        Dispatch(client_id, packet_type, packet);
    }

private:
    std::unordered_map<unsigned char, PacketHandler> handlers_;
};

extern PacketHandlerManager g_packet_handler;

class EXP_OVER {
public:
    WSAOVERLAPPED   _wsa_over;
    COMP_OP         _comp_op;
    WSABUF         _wsa_buf;
    unsigned char   _net_buf[BUFSIZE];
    int            _target;
public:
    EXP_OVER(COMP_OP comp_op, char num_bytes, void* mess) : _comp_op(comp_op)
    {
        ZeroMemory(&_wsa_over, sizeof(_wsa_over));
        _wsa_buf.buf = reinterpret_cast<char*>(_net_buf);
        _wsa_buf.len = num_bytes;
        memcpy(_net_buf, mess, num_bytes);
    }

    EXP_OVER(COMP_OP comp_op) : _comp_op(comp_op) {}

    EXP_OVER()
    {
        _comp_op = OP_RECV;
    }

    ~EXP_OVER()
    {
    }
};

// ============================================================
// [TODO: Grid System] - 나중에 적용 예정
//
// 목표: 전체 clients 순회(O(N)) → 인접 셀 탐색(O(k))으로 교체
//
// 설계안:
//   CELL_SIZE  = 16       (타일 단위, RANGE=15 기준)
//   GRID_W     = WORLD_WIDTH  / CELL_SIZE   = 125
//   GRID_H     = WORLD_HEIGHT / CELL_SIZE   = 125
//
//   struct GridCell {
//       mutex                  mtx;
//       unordered_set<int>     players;   // 플레이어 ID
//       unordered_set<int>     npcs;      // NPC ID
//   };
//   GridCell g_grid[GRID_H][GRID_W];
//
// 적용 위치:
//   - do_npc_move()   : old/new_viewlist 수집 시 인접 셀만 탐색
//   - handle_move()   : nearlist 수집 시 인접 셀만 탐색
//   - Initialize_NPC(): 초기화 시 셀에 NPC 등록
//   - Disconnect()    : 셀에서 플레이어 제거
//
// 적용 순서:
//   1. GridCell 구조체 + g_grid 배열 선언
//   2. 플레이어/NPC 이동 시 셀 이동 처리 (erase → insert)
//   3. do_npc_move, handle_move의 순회 로직을 셀 기반으로 교체
// ============================================================

struct timer_event {
    int obj_id;
    chrono::system_clock::time_point   start_time;
    EVENT_TYPE ev;
    int target_id;
    constexpr bool operator < (const timer_event& _Left) const
    {
        return (start_time > _Left.start_time);
    }

};