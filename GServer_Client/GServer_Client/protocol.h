#pragma once

#include "game_protocol_generated.h"
#include <flatbuffers/flatbuffers.h>

// Legacy constants
const short SERVER_PORT = 4000;
const int  WORLD_HEIGHT = 2000;
const int  WORLD_WIDTH = 2000;
const int  MAX_NAME_SIZE = 20;
const int  MAX_CHAT_SIZE = 100;
const int  MAX_USER = 10000;
const int  MAX_NPC = 200000;
constexpr int NPC_ID_START = MAX_USER;
constexpr int NPC_ID_END = MAX_USER + MAX_NPC - 1;

// Packet type constants for compatibility
const char CS_PACKET_LOGIN = 1;
const char CS_PACKET_MOVE = 2;
const char CS_PACKET_ATTACK = 3;
const char CS_PACKET_CHAT = 4;
const char CS_PACKET_TELEPORT = 5;

const char SC_PACKET_LOGIN_OK = 1;
const char SC_PACKET_MOVE = 2;
const char SC_PACKET_PUT_OBJECT = 3;
const char SC_PACKET_REMOVE_OBJECT = 4;
const char SC_PACKET_CHAT = 5;
const char SC_PACKET_LOGIN_FAIL = 6;
const char SC_PACKET_STATUS_CHANGE = 7;
const char SC_PACKET_OBSTACLE = 8;
const char SC_PACKET_ATTACK = 9;
const char SC_PACKET_LOGIN_NO = 10;

namespace FBProtocol {
    // FlatBuffers builder type alias
    using Builder = flatbuffers::FlatBufferBuilder;
    
    // Helper functions for packet creation
    inline flatbuffers::Offset<GameProtocol::CSMessage> CreateLoginPacket(
        Builder& builder, const char* name)
    {
        auto name_str = builder.CreateString(name);
        auto login = GameProtocol::CreateCSLogin(builder, name_str);
        auto msg = GameProtocol::CreateCSMessage(builder, 
            GameProtocol::CSPacket_CSLogin, login.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::CSMessage> CreateMovePacket(
        Builder& builder, char direction, int move_time)
    {
        GameProtocol::Direction dir = static_cast<GameProtocol::Direction>(direction);
        auto move = GameProtocol::CreateCSMove(builder, dir, move_time);
        auto msg = GameProtocol::CreateCSMessage(builder,
            GameProtocol::CSPacket_CSMove, move.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::CSMessage> CreateAttackPacket(
        Builder& builder, int target_id)
    {
        auto attack = GameProtocol::CreateCSAttack(builder, target_id);
        auto msg = GameProtocol::CreateCSMessage(builder,
            GameProtocol::CSPacket_CSAttack, attack.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::CSMessage> CreateChatPacket(
        Builder& builder, const char* message)
    {
        auto msg_str = builder.CreateString(message);
        auto chat = GameProtocol::CreateCSChat(builder, msg_str);
        auto msg = GameProtocol::CreateCSMessage(builder,
            GameProtocol::CSPacket_CSChat, chat.Union());
        return msg;
    }
    
    // Server to Client packet creators
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateLoginOkPacket(
        Builder& builder, int id, short x, short y, short level, 
        short hp, short maxhp, int exp)
    {
        auto login_ok = GameProtocol::CreateSCLoginOk(builder, 
            id, x, y, level, hp, maxhp, exp);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCLoginOk, login_ok.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateMovePacket(
        Builder& builder, int id, int hp, short x, short y, int move_time)
    {
        auto move = GameProtocol::CreateSCMove(builder, id, hp, x, y, move_time);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCMove, move.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreatePutObjectPacket(
        Builder& builder, int id, short x, short y, int hp, 
        char object_type, const char* name)
    {
        auto name_str = builder.CreateString(name);
        GameProtocol::ObjectType obj_type = 
            static_cast<GameProtocol::ObjectType>(object_type);
        auto put_obj = GameProtocol::CreateSCPutObject(builder, 
            id, x, y, hp, obj_type, name_str);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCPutObject, put_obj.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateRemoveObjectPacket(
        Builder& builder, int id)
    {
        auto remove_obj = GameProtocol::CreateSCRemoveObject(builder, id);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCRemoveObject, remove_obj.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateChatPacket(
        Builder& builder, int id, const char* message)
    {
        auto msg_str = builder.CreateString(message);
        auto chat = GameProtocol::CreateSCChat(builder, id, msg_str);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCChat, chat.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateAttackPacket(
        Builder& builder, int id, short x, short y, short hp, int exp)
    {
        auto attack = GameProtocol::CreateSCAttack(builder, id, x, y, hp, exp);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCAttack, attack.Union());
        return msg;
    }
    
    inline flatbuffers::Offset<GameProtocol::SCMessage> CreateLoginFailPacket(
        Builder& builder, int reason)
    {
        auto login_fail = GameProtocol::CreateSCLoginFail(builder, reason);
        auto msg = GameProtocol::CreateSCMessage(builder,
            GameProtocol::SCPacket_SCLoginFail, login_fail.Union());
        return msg;
    }
}

// Legacy struct definitions for backward compatibility
// These will be gradually phased out
#pragma pack (push, 1)
struct cs_packet_login {
    unsigned char size;
    char    type;
    char    name[MAX_NAME_SIZE];
};

struct cs_packet_move {
    unsigned char size;
    char    type;
    char    direction;
    int     move_time;
};

struct cs_packet_attack {
    unsigned char size;
    char    type;
    int id;
};

struct cs_packet_chat {
    unsigned char size;
    char    type;
    char    message[MAX_CHAT_SIZE];
};

struct sc_packet_login_ok {
    unsigned char size;
    char type;
    int     id;
    short   x, y;
    short   level;
    short   hp, maxhp;
    int     exp;
};

struct sc_packet_move {
    unsigned char size;
    char type;
    int     id;
    int hp;
    short  x, y;
    int     move_time;
};

struct sc_packet_put_object {
    unsigned char size;
    char type;
    int id;
    short x, y;
    int hp;
    char object_type;
    char    name[MAX_NAME_SIZE];
};

struct sc_packet_remove_object {
    unsigned char size;
    char type;
    int id;
};

struct sc_packet_chat {
    unsigned char size;
    char type;
    int id;
    char message[MAX_CHAT_SIZE];
};

struct sc_packet_attack {
    unsigned char size;
    char type;
    int id;
    short x, y;
    short hp;
    int exp;
};
#pragma pack(pop)
