#pragma once
#include "stdafx.h"

void error_display(int err_no);

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