#include "Player.h"
#include "ExpOverPool.h"

void Player::do_send(int num_bytes, void* mess)
{
    EXP_OVER* ex_over = g_ExpOverPool.Acquire();
    ZeroMemory(&ex_over->_wsa_over, sizeof(ex_over->_wsa_over));
    ex_over->_comp_op     = OP_SEND;
    ex_over->_wsa_buf.buf = reinterpret_cast<char*>(ex_over->_net_buf);
    ex_over->_wsa_buf.len = static_cast<ULONG>(num_bytes);
    memcpy(ex_over->_net_buf, mess, num_bytes);

    int ret = WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
    if (SOCKET_ERROR == ret)
    {
        int error_num = WSAGetLastError();
        if (ERROR_IO_PENDING != error_num)
        {
            error_display(error_num);
            g_ExpOverPool.Release(ex_over);
        }
        // IO_PENDING → IOCP 완료 통지(OP_SEND)에서 Release
    }
}

void Player::do_recv()
{
    DWORD recv_flag = 0;
    ZeroMemory(&_recv_over._wsa_over, sizeof(_recv_over._wsa_over));
    _recv_over._wsa_buf.buf = reinterpret_cast<char*>(_recv_over._net_buf + _prev_size);
    _recv_over._wsa_buf.len = sizeof(_recv_over._net_buf) - _prev_size;

    int ret = WSARecv(_socket, &_recv_over._wsa_buf, 1, 0,
                      &recv_flag, &_recv_over._wsa_over, NULL);
    if (SOCKET_ERROR == ret)
    {
        int error_num = WSAGetLastError();
        if (ERROR_IO_PENDING != error_num)
            error_display(error_num);
    }
}
