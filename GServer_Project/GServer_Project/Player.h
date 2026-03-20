#pragma once
#include "BaseObject.h"

// -----------------------------------------------------------------------
// Player : BaseObject
//   소켓/IOCP 수신에 필요한 필드만 추가.
//   NPC에는 없는 필드: _recv_over, _socket, _prev_size, last_move_time
// -----------------------------------------------------------------------
class Player : public BaseObject
{
public:
    EXP_OVER _recv_over;
    SOCKET   _socket        = INVALID_SOCKET;
    int      _prev_size     = 0;
    int      last_move_time = 0;

    Player() { x = 0; y = 0; }

    ~Player() override
    {
        if (_state != ST_FREE)
            closesocket(_socket);
    }

    void do_recv();
    void do_send(int num_bytes, void* mess);

    // setter 헬퍼
    void set_id    (int id)    { _id   = id;   }
    void set_hp    (short v)   { hp    = v;    }
    void set_level (short v)   { level = v;    }
    void set_exp   (short v)   { exp   = v;    }
    void set_maxhp (short v)   { maxhp = v;    }
};
