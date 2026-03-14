#pragma once
#include "stdafx.h"
#include <unordered_map>

void error_display(int err_no);

// 패킷 핸들러 함수 포인터 타입
// void handler(int client_id, const uint8_t* fb_data, uint32_t fb_size)
// fb_data: messegeid 4바이트 이후의 순수 FlatBuffers 데이터
// fb_size: FlatBuffers 데이터 크기
using PacketHandler = std::function<void(int, const uint8_t*, uint32_t)>;

// 패킷 타입별 핸들러를 등록하고 디스패치하는 매니저
// 키: EPacketProtocol 값 (int32_t)
// 수신 포맷: [4바이트 messegeid][FlatBuffers 데이터]
class PacketHandlerManager {
public:
    // EPacketProtocol 값(int32)으로 핸들러 등록
    void RegisterHandler(int32_t messege_id, PacketHandler handler)
    {
        handlers_[messege_id] = handler;
    }

    // messegeid와 FlatBuffers 버퍼를 받아 핸들러 호출
    void Dispatch(int client_id, int32_t messege_id,
                  const uint8_t* fb_data, uint32_t fb_size) const
    {
        auto it = handlers_.find(messege_id);
        if (it != handlers_.end()) {
            it->second(client_id, fb_data, fb_size);
        } else {
            cout << "[PacketHandlerManager] Unknown messegeid: "
                 << messege_id << endl;
        }
    }

private:
    std::unordered_map<int32_t, PacketHandler> handlers_;
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