#pragma once

#include "game_protocol_generated.h"
#include <flatbuffers/flatbuffers.h>

// -----------------------------------------------------------------------
// Server constants
// -----------------------------------------------------------------------
const short SERVER_PORT    = 4000;
const int   WORLD_HEIGHT   = 2000;
const int   WORLD_WIDTH    = 2000;
const int   MAX_NAME_SIZE  = 20;
const int   MAX_CHAT_SIZE  = 100;
const int   MAX_USER       = 10000;
const int   MAX_NPC        = 200000;
constexpr int NPC_ID_START = MAX_USER;
constexpr int NPC_ID_END   = MAX_USER + MAX_NPC - 1;

// -----------------------------------------------------------------------
// CS packet type constants (matches CSPacketType enum in .fbs)
// -----------------------------------------------------------------------
const uint8_t CS_PACKET_LOGIN    = 1;
const uint8_t CS_PACKET_MOVE     = 2;
const uint8_t CS_PACKET_ATTACK   = 3;
const uint8_t CS_PACKET_CHAT     = 4;
const uint8_t CS_PACKET_TELEPORT = 5;

// -----------------------------------------------------------------------
// SC packet type constants (matches SCPacketType enum in .fbs)
// -----------------------------------------------------------------------
const uint8_t SC_PACKET_LOGIN_OK       = 1;
const uint8_t SC_PACKET_MOVE           = 2;
const uint8_t SC_PACKET_PUT_OBJECT     = 3;
const uint8_t SC_PACKET_REMOVE_OBJECT  = 4;
const uint8_t SC_PACKET_CHAT           = 5;
const uint8_t SC_PACKET_LOGIN_FAIL     = 6;
const uint8_t SC_PACKET_STATUS_CHANGE  = 7;
const uint8_t SC_PACKET_OBSTACLE       = 8;
const uint8_t SC_PACKET_ATTACK         = 9;
const uint8_t SC_PACKET_LOGIN_NO       = 10;
const uint8_t SC_PACKET_TELEPORT       = 11;

// -----------------------------------------------------------------------
// FBProtocol namespace: FlatBuffers 직렬화/역직렬화 헬퍼
//
// 전송 포맷: [4바이트 little-endian 크기][FlatBuffers 바이트열]
//   - do_send(buf)를 호출할 때 builder.GetBufferPointer() 앞에
//     4바이트 크기 헤더를 붙여서 보냅니다.
//
// 수신 포맷: 동일하게 [4바이트 크기][FlatBuffers 바이트열]
//   - OP_RECV 핸들러에서 앞 4바이트를 크기로 읽어 분리한 뒤
//     GetCSMessage()로 역직렬화합니다.
// -----------------------------------------------------------------------
namespace FBProtocol {

using Builder = flatbuffers::FlatBufferBuilder;

// -----------------------------------------------------------------------
// Helper: FlatBuffers 버퍼를 [4바이트 크기 헤더 + 데이터] 형태로 전송
//   반환값: {헤더+데이터 합친 vector<uint8_t>}
//   이 vector를 do_send(size, data)에 넘기면 됩니다.
// -----------------------------------------------------------------------
inline std::vector<uint8_t> FinishAndFrame(Builder& builder,
                                            flatbuffers::Offset<GameProtocol::SCMessage> root)
{
    builder.Finish(root);
    const uint8_t* buf  = builder.GetBufferPointer();
    uint32_t        size = builder.GetSize();

    std::vector<uint8_t> framed(4 + size);
    framed[0] = static_cast<uint8_t>(size & 0xFF);
    framed[1] = static_cast<uint8_t>((size >> 8)  & 0xFF);
    framed[2] = static_cast<uint8_t>((size >> 16) & 0xFF);
    framed[3] = static_cast<uint8_t>((size >> 24) & 0xFF);
    std::memcpy(framed.data() + 4, buf, size);
    return framed;
}

// -----------------------------------------------------------------------
// CS 메시지 역직렬화
// -----------------------------------------------------------------------
inline const GameProtocol::CSMessage* ParseCSMessage(const uint8_t* data, uint32_t size)
{
    flatbuffers::Verifier verifier(data, size);
    if (!GameProtocol::VerifyCSMessageBuffer(verifier)) return nullptr;
    return GameProtocol::GetCSMessage(data);
}

// -----------------------------------------------------------------------
// SC → Client 패킷 빌더 함수들
// -----------------------------------------------------------------------

inline std::vector<uint8_t> BuildLoginOk(int id, short x, short y,
                                          short level, short hp, short maxhp, int exp)
{
    Builder builder;
    auto login_ok = GameProtocol::CreateSCLoginOk(builder, id, x, y, level, hp, maxhp, exp);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCLoginOk, login_ok.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildLoginFail(const char* reason)
{
    Builder builder;
    auto reason_str = builder.CreateString(reason ? reason : "");
    auto login_fail = GameProtocol::CreateSCLoginFail(builder, reason_str);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCLoginFail, login_fail.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildMove(int id, short hp, short x, short y, uint32_t move_time)
{
    Builder builder;
    auto move = GameProtocol::CreateSCMove(builder, id, hp, x, y, move_time);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCMove, move.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildPutObject(int id, short x, short y, short hp,
                                             GameProtocol::ObjectType obj_type, const char* name)
{
    Builder builder;
    auto name_str = builder.CreateString(name ? name : "");
    auto put = GameProtocol::CreateSCPutObject(builder, id, x, y, hp, obj_type, name_str);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCPutObject, put.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildRemoveObject(int id)
{
    Builder builder;
    auto remove = GameProtocol::CreateSCRemoveObject(builder, id);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCRemoveObject, remove.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildChat(int id, const char* message)
{
    Builder builder;
    auto msg_str = builder.CreateString(message ? message : "");
    auto chat = GameProtocol::CreateSCChat(builder, id, msg_str);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCChat, chat.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildAttack(int id, short x, short y, short hp, int exp)
{
    Builder builder;
    auto attack = GameProtocol::CreateSCAttack(builder, id, x, y, hp, exp);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCAttack, attack.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildStatusChange(short level, short hp, short maxhp, int exp)
{
    Builder builder;
    auto status = GameProtocol::CreateSCStatusChange(builder, level, hp, maxhp, exp);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCStatusChange, status.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildObstacle(int id, short x, short y)
{
    Builder builder;
    auto obstacle = GameProtocol::CreateSCObstacle(builder, id, x, y);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCObstacle, obstacle.Union());
    return FinishAndFrame(builder, msg);
}

inline std::vector<uint8_t> BuildTeleport(int id, short x, short y)
{
    Builder builder;
    auto teleport = GameProtocol::CreateSCTeleport(builder, id, x, y);
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCTeleport, teleport.Union());
    return FinishAndFrame(builder, msg);
}

} // namespace FBProtocol

// -----------------------------------------------------------------------
// Legacy struct definitions (kept for reference only - DO NOT use in new code)
// -----------------------------------------------------------------------
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

struct sc_packet_obstacle {
    unsigned char size;
    char type;
    int id;
    short x, y;
};
#pragma pack(pop)
