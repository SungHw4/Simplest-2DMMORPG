#pragma once

#include "game_protocol_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <vector>
#include <cstdint>
#include <cstring>

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

// -----------------------------------------------------------------------
// FBProtocol 네임스페이스
//
// 패킷 포맷 (CS/SC 공통):
//   [4바이트 messegeid (int32 LE)][FlatBuffers 직렬화 데이터]
//
// CSLogin 예외:
//   CSLogin 테이블에는 messegeid 필드가 없으므로
//   [4바이트 CS_LOGIN_REQUEST(101)][CSLogin FlatBuffers 데이터]
//   로 처리합니다.
// -----------------------------------------------------------------------
namespace FBProtocol {

using Builder = flatbuffers::FlatBufferBuilder;

// -----------------------------------------------------------------------
// Helper: 직렬화된 FlatBuffers 버퍼 앞에 4바이트 messegeid 헤더를 붙여 반환
// -----------------------------------------------------------------------
inline std::vector<uint8_t> Frame(int32_t messege_id,
                                   const uint8_t* fb_buf,
                                   uint32_t fb_size)
{
    std::vector<uint8_t> framed(4 + fb_size);
    framed[0] = static_cast<uint8_t>(messege_id & 0xFF);
    framed[1] = static_cast<uint8_t>((messege_id >> 8)  & 0xFF);
    framed[2] = static_cast<uint8_t>((messege_id >> 16) & 0xFF);
    framed[3] = static_cast<uint8_t>((messege_id >> 24) & 0xFF);
    std::memcpy(framed.data() + 4, fb_buf, fb_size);
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

// SC_LoginResponse (104): 로그인 성공 → 클라이언트에게 캐릭터 정보 전송
// 새 프로토콜에 SCLoginOk 테이블이 없으므로 CSLogin 응답으로
// 위치 정보는 SCPlayerMoveResponse를 활용하거나 별도 테이블로 처리.
// 여기서는 로그인 응답에 필요한 기본 정보를 SCPlayerMoveResponse로 전달.
// (향후 서버에서 별도 SCLoginResponse 테이블 추가 권장)
inline std::vector<uint8_t> BuildLoginResponse(
    GameProtocol::Direction dir, int32_t msg_id = SC_LOGIN_RESPONSE)
{
    Builder builder;
    auto resp = GameProtocol::CreateSCPlayerMoveResponse(
        builder, dir,
        static_cast<GameProtocol::EPacketProtocol>(SC_LOGIN_RESPONSE));
    builder.Finish(resp);
    return Frame(SC_LOGIN_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// SC_PlayerMoveResponse (202): 플레이어/NPC 이동 브로드캐스트
inline std::vector<uint8_t> BuildMoveResponse(
    GameProtocol::Direction dir)
{
    Builder builder;
    auto resp = GameProtocol::CreateSCPlayerMoveResponse(
        builder, dir,
        GameProtocol::EPacketProtocol_SC_PlayerMoveResponse);
    builder.Finish(resp);
    return Frame(SC_PLAYER_MOVE_RESPONSE,
                 builder.GetBufferPointer(), builder.GetSize());
}

// SC_PlayerAttackResponse (206): 공격 결과
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

// SC_PlayerChattingResponse (302): 채팅 메시지 브로드캐스트
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
// CS 패킷 역직렬화 헬퍼
// -----------------------------------------------------------------------

// CSLogin 역직렬화 (messegeid 없음, fb_data가 CSLogin 버퍼)
inline const GameProtocol::CSLogin* ParseCSLogin(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSLogin>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSLogin>(fb_data);
}

// CSPlayerMoveRequest 역직렬화
inline const GameProtocol::CSPlayerMoveRequest* ParseCSPlayerMoveRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerMoveRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerMoveRequest>(fb_data);
}

// CSPlayerAttackRequest 역직렬화
inline const GameProtocol::CSPlayerAttackRequest* ParseCSPlayerAttackRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerAttackRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerAttackRequest>(fb_data);
}

// CSPlayerChattingRequest 역직렬화
inline const GameProtocol::CSPlayerChattingRequest* ParseCSPlayerChattingRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSPlayerChattingRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSPlayerChattingRequest>(fb_data);
}

// CSRandomTeleportRequest 역직렬화
inline const GameProtocol::CSRandomTeleportRequest* ParseCSRandomTeleportRequest(
    const uint8_t* fb_data, uint32_t fb_size)
{
    flatbuffers::Verifier v(fb_data, fb_size);
    if (!v.VerifyBuffer<GameProtocol::CSRandomTeleportRequest>()) return nullptr;
    return flatbuffers::GetRoot<GameProtocol::CSRandomTeleportRequest>(fb_data);
}

} // namespace FBProtocol
