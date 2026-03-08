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
    void Dispatch(int client_id, unsigned char* packet) const
    {
        unsigned char packet_type = packet[1];
        auto it = handlers_.find(packet_type);
        if (it != handlers_.end()) {
            it->second(client_id, packet);
        } else {
            cout << "[PacketHandlerManager] Unknown packet type: "
                 << static_cast<int>(packet_type) << endl;
        }
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