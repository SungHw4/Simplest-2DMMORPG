#pragma once

#include "game_protocol_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <vector>
#include <cstdint>
#include <cstring>
//#include "2021_Proto.h"
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
// EPacketProtocol 값 (EPacketProtocol enum과 동일, 편의용 상수)
// -----------------------------------------------------------------------
constexpr int32_t CS_LOGIN_REQUEST          = 101;
constexpr int32_t SC_LOGIN_RESPONSE         = 104;

constexpr int32_t CS_PLAYER_MOVE_REQUEST    = 201;
constexpr int32_t SC_PLAYER_MOVE_RESPONSE   = 202;

constexpr int32_t CS_PLAYER_TELEPORT_REQUEST  = 203;
constexpr int32_t SC_PLAYER_TELEPORT_RESPONSE = 204;

constexpr int32_t CS_PLAYER_ATTACK_REQUEST  = 205;
constexpr int32_t SC_PLAYER_ATTACK_RESPONSE = 206;

constexpr int32_t CS_PLAYER_CHATTING_REQUEST  = 301;
constexpr int32_t SC_PLAYER_CHATTING_RESPONSE = 302;

constexpr int32_t CS_RANDOM_TELEPORT_REQUEST  = 401;

constexpr int32_t SC_INTEGRATION_ERROR_NOTIFICATION = 500;

// -----------------------------------------------------------------------
// FBProtocol 네임스페이스
//
// 패킷 포맷 (CS/SC 공통):
//   [4바이트 messegeid (int32 LE)][FlatBuffers 직렬화 데이터]
// -----------------------------------------------------------------------
namespace FBProtocol {

using Builder = flatbuffers::FlatBufferBuilder;

// -----------------------------------------------------------------------
// Helper: 직렬화된 FlatBuffers 버퍼 앞에 8바이트 헤더를 붙여 반환
//   포맷: [4바이트 messegeid (LE)][4바이트 fb_size (LE)][fb_data]
//   서버 OP_RECV 수신 포맷과 동일하게 맞춤 (CS/SC 공통)
// -----------------------------------------------------------------------
inline std::vector<uint8_t> Frame(int32_t messege_id,
                                   const uint8_t* fb_buf,
                                   uint32_t fb_size)
{
    std::vector<uint8_t> framed(8 + fb_size);
    // messegeid (4 bytes LE)
    framed[0] = static_cast<uint8_t>(messege_id & 0xFF);
    framed[1] = static_cast<uint8_t>((messege_id >> 8)  & 0xFF);
    framed[2] = static_cast<uint8_t>((messege_id >> 16) & 0xFF);
    framed[3] = static_cast<uint8_t>((messege_id >> 24) & 0xFF);
    // fb_size (4 bytes LE)
    framed[4] = static_cast<uint8_t>(fb_size & 0xFF);
    framed[5] = static_cast<uint8_t>((fb_size >> 8)  & 0xFF);
    framed[6] = static_cast<uint8_t>((fb_size >> 16) & 0xFF);
    framed[7] = static_cast<uint8_t>((fb_size >> 24) & 0xFF);
    std::memcpy(framed.data() + 8, fb_buf, fb_size);
    return framed;
}

// -----------------------------------------------------------------------
// 수신 버퍼에서 messegeid 추출 (앞 4바이트)
// -----------------------------------------------------------------------
inline int32_t ReadMessegeId(const uint8_t* buf)
{
    return static_cast<int32_t>(
        static_cast<uint32_t>(buf[0])        |
        (static_cast<uint32_t>(buf[1]) << 8)  |
        (static_cast<uint32_t>(buf[2]) << 16) |
        (static_cast<uint32_t>(buf[3]) << 24));
}

// -----------------------------------------------------------------------
// SC → Client 패킷 빌더
// -----------------------------------------------------------------------

// SC_LoginResponse (104)
inline std::vector<uint8_t> BuildLoginResponse(GameProtocol::Direction dir)
{
    Builder builder;
    auto resp = GameProtocol::CreateSCPlayerMoveResponse(
        builder, dir,
        static_cast<GameProtocol::EPacketProtocol>(SC_LOGIN_RESPONSE));
    builder.Finish(resp);
    return Frame(SC_LOGIN_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// SC_PlayerMoveResponse (202)
inline std::vector<uint8_t> BuildMoveResponse(GameProtocol::Direction dir)
{
    Builder builder;
    auto resp = GameProtocol::CreateSCPlayerMoveResponse(
        builder, dir,
        GameProtocol::EPacketProtocol_SC_PlayerMoveResponse);
    builder.Finish(resp);
    return Frame(SC_PLAYER_MOVE_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// SC_PlayerAttackResponse (206)
inline std::vector<uint8_t> BuildAttackResponse(
    int target_id, GameProtocol::Direction dir, int hp, int exp)
{
    Builder builder;
    auto resp = GameProtocol::CreateSCPlayerAttackResponse(
        builder, target_id, dir, hp, exp,
        GameProtocol::EPacketProtocol_SC_PlayerAttackResponse);
    builder.Finish(resp);
    return Frame(SC_PLAYER_ATTACK_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// SC_PlayerChattingResponse (302)
inline std::vector<uint8_t> BuildChattingResponse(
    int sender_id, const char* message)
{
    Builder builder;
    auto msg_str = builder.CreateString(message ? message : "");
    auto resp = GameProtocol::CreateSCPlayerChattingResponse(
        builder, sender_id, msg_str,
        GameProtocol::EPacketProtocol_SC_PlayerChattingResponse);
    builder.Finish(resp);
    return Frame(SC_PLAYER_CHATTING_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// -----------------------------------------------------------------------
// SC_IntegrationErrorNotification (500)
//   에러 발생 시 클라이언트에게 보내는 통합 에러 알림 패킷.
//
//   사용 방법 (GameService 에서):
//     _SendError(clientID,
//                EPacketProtocol_CS_PlayerMoveRequest,
//                EErrorMsg_EF_FAIL_WRONG_REQ);
//
//   패킷 구조:
//     messageid : 에러가 발생한 원래 요청 패킷의 EPacketProtocol ID
//     errorcode : EErrorMsg 값
// -----------------------------------------------------------------------
inline std::vector<uint8_t> BuildIntegrationError(
    int32_t                   originMessageID,
    GameProtocol::EErrorMsg   err)
{
    Builder builder;
    auto packet = GameProtocol::CreateSCIntegrationErrorNotification(
        builder,
        originMessageID,
        static_cast<int32_t>(err));
    builder.Finish(packet);
    return Frame(SC_INTEGRATION_ERROR_NOTIFICATION,
                 builder.GetBufferPointer(), builder.GetSize());
}

// -----------------------------------------------------------------------
// CS 패킷 역직렬화 헬퍼
// -----------------------------------------------------------------------

inline const GameProtocol::CSLogin* ParseCSLogin(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSLogin>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSLogin>(fb_data);
}

inline const GameProtocol::CSPlayerMoveRequest* ParseCSPlayerMoveRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerMoveRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerMoveRequest>(fb_data);
}

inline const GameProtocol::CSPlayerAttackRequest* ParseCSPlayerAttackRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerAttackRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerAttackRequest>(fb_data);
}

inline const GameProtocol::CSPlayerChattingRequest* ParseCSPlayerChattingRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerChattingRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerChattingRequest>(fb_data);
}

inline const GameProtocol::CSRandomTeleportRequest* ParseCSRandomTeleportRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSRandomTeleportRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSRandomTeleportRequest>(fb_data);
}

} // namespace FBProtocol
