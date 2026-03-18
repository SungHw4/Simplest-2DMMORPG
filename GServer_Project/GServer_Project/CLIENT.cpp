#include "CLIENT.h"
#include "ExpOverPool.h"

void CLIENT::do_send(int num_bytes, void* mess)
{
    EXP_OVER* ex_over = g_ExpOverPool.Acquire();
    ZeroMemory(&ex_over->_wsa_over, sizeof(ex_over->_wsa_over));
    ex_over->_comp_op     = OP_SEND;
    ex_over->_wsa_buf.buf = reinterpret_cast<char*>(ex_over->_net_buf);  // 내부 버퍼 사용
    ex_over->_wsa_buf.len = static_cast<ULONG>(num_bytes);
    memcpy(ex_over->_net_buf, mess, num_bytes);  // 데이터 복사 (비동기 완료 전 호출자 버퍼 해제 대비)

    int ret = WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
    if (SOCKET_ERROR == ret) {
        int error_num = WSAGetLastError();
        if (ERROR_IO_PENDING != error_num) {
            error_display(error_num);
            g_ExpOverPool.Release(ex_over);  // 전송 실패 시 즉시 반납
        }
        // IO_PENDING이면 IOCP 완료 통지(OP_SEND case)에서 Release
    }
}

void CLIENT::do_recv()
{
    DWORD recv_flag = 0;
    ZeroMemory(&_recv_over._wsa_over, sizeof(_recv_over._wsa_over));
    _recv_over._wsa_buf.buf = reinterpret_cast<char*>(_recv_over._net_buf + _prev_size);
    _recv_over._wsa_buf.len = sizeof(_recv_over._net_buf) - _prev_size;
    int ret = WSARecv(_socket, &_recv_over._wsa_buf, 1, 0, &recv_flag, &_recv_over._wsa_over, NULL);
    if (SOCKET_ERROR == ret) {
        int error_num = WSAGetLastError();
        if (ERROR_IO_PENDING != error_num)
            error_display(error_num);
    }
}
int CLIENT::get_id()
{
    return _id;
}
void CLIENT::set_hp(short hp)
{
    this->hp = hp;
}

void CLIENT::set_id(int id)
{
    this->_id = id;
}
void CLIENT::set_level(short level)
{
    this->level = level;
}

void CLIENT::set_exp(short exp)
{
    this->exp = exp;
}
void CLIENT::set_maxhp(short maxhp)
{
    this->maxhp = maxhp;
}

STATE CLIENT::get_state()
{
    return _state;
}
void CLIENT::set_active(bool active)
{
    _is_active = active;
}


