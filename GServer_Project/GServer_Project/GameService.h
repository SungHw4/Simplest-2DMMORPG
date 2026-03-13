#pragma once
#include "Service.h"
#include "game_protocol_generated.h"
#include "protocol.h"
#include "InnerPacket.h"

// 전방 선언
class GameDBService;
extern array<CLIENT, MAX_USER + MAX_NPC> clients;
extern BOOL obs[WORLD_HEIGHT][WORLD_WIDTH];

// -----------------------------------------------------------------------
// GameService
//   FSCore Service 패턴을 적용한 인게임 행동 처리 서비스.
//
//   역할:
//     - CS 패킷(로그인/이동/공격/채팅/텔레포트)을 수신하여 인게임 로직 수행
//     - DB가 필요한 요청(로그인)은 InnerPacket으로 GameDBService에 위임
//     - GameDBService 결과(DB_LoginResponse)를 InnerPacket으로 수신하여 처리
//
//   흐름:
//     IOCP worker
//       -> g_GameService.Push(Packet)
//         -> GameService 스레드: RegisterHandler로 등록된 핸들러 호출
//           -> (DB 필요 시) g_DBService.Push(InnerPacket)
//             -> GameDBService 스레드: DB 작업 수행
//               -> g_GameService.InnerPush(InnerPacket) 결과 전달
//                 -> GameService 스레드: RegisterInnerHandler 핸들러 호출
// -----------------------------------------------------------------------
class GameService : public Service
{
public:
    GameService();
    ~GameService() override = default;

    // 다른 서비스 참조 주입 (main에서 초기화 후 설정)
    void SetDBService(GameDBService* pDBService) { mpDBService = pDBService; }

private:
    GameDBService* mpDBService = nullptr;

    // -----------------------------------------------------------------------
    // CS 패킷 핸들러
    // -----------------------------------------------------------------------
    bool Handle_Login   (int clientID, const GameProtocol::CSLogin& msg);
    bool Handle_Move    (int clientID, const GameProtocol::CSPlayerMoveRequest& msg);
    bool Handle_Attack  (int clientID, const GameProtocol::CSPlayerAttackRequest& msg);
    bool Handle_Chatting(int clientID, const GameProtocol::CSPlayerChattingRequest& msg);
    bool Handle_RandomTeleport(int clientID, const GameProtocol::CSRandomTeleportRequest& msg);

    // -----------------------------------------------------------------------
    // InnerPacket 핸들러 (GameDBService -> GameService 결과 수신)
    // -----------------------------------------------------------------------
    bool Handle_DB_LoginResponse(InnerPacket::SharedPtr pInner);

    // -----------------------------------------------------------------------
    // 유틸리티
    // -----------------------------------------------------------------------
    void _SendFB(int clientID, std::vector<uint8_t>& framed);
    void _SendNak_Move    (int clientID, GameProtocol::EErrorMsg err);
    void _SendNak_Attack  (int clientID, GameProtocol::EErrorMsg err);
    void _SendNak_Chatting(int clientID, GameProtocol::EErrorMsg err);
};
