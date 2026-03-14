#pragma once
#include "Service.h"
#include "game_protocol_generated.h"
#include "protocol.h"
#include "InnerPacket.h"
#include "CLIENT.h"   // CLIENT 클래스 정의 — extern array<CLIENT,...> 및 is_player 등 사용에 필요

// 전방 선언
class GameDBService;
extern array<CLIENT, MAX_USER + MAX_NPC> clients;
extern BOOL obs[WORLD_HEIGHT][WORLD_WIDTH];

// 2021g_Server.cpp 에 정의된 유틸리티 함수 선언
// GameService.cpp 에서 is_player / is_npc / is_attack_range 를 사용하므로
// 링커 오류를 막기 위해 여기서 선언한다.
bool is_player(int id);
bool is_npc(int id);
bool is_attack_range(int a, int b);

// -----------------------------------------------------------------------
// GameService
//   FSCore Service 패턴을 적용한 인게임 행동 처리 서비스.
//
//   역할:
//     - CS 패킷(로그인/이동/공격/채팅/텔레포트)을 수신하여 인게임 로직 수행
//     - DB 가 필요한 요청(로그인)은 InnerPacket 으로 GameDBService 에 위임
//     - GameDBService 결과(DB_LoginResponse)를 InnerPacket 으로 수신하여 처리
//
//   에러 처리:
//     각 핸들러에서 에러 발생 시 _SendError(originMsgID, EErrorMsg) 를 호출.
//     → FBProtocol::BuildIntegrationError() 로 SCIntegrationErrorNotification(500)
//       패킷을 생성해 해당 클라이언트에게만 전송한다.
//
//   흐름:
//     IOCP worker
//       -> g_GameService.Push(Packet)
//         -> GameService 스레드: RegisterHandler 로 등록된 핸들러 호출
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

    // 다른 서비스 참조 주입 (main 에서 초기화 후 설정)
    void SetDBService(GameDBService* pDBService) { mpDBService = pDBService; }

private:
    GameDBService* mpDBService = nullptr;

    // -----------------------------------------------------------------------
    // CS 패킷 핸들러
    // -----------------------------------------------------------------------
    bool Handle_Login          (int clientID, const GameProtocol::CSLogin& msg);
    bool Handle_Move           (int clientID, const GameProtocol::CSPlayerMoveRequest& msg);
    bool Handle_Attack         (int clientID, const GameProtocol::CSPlayerAttackRequest& msg);
    bool Handle_Chatting       (int clientID, const GameProtocol::CSPlayerChattingRequest& msg);
    bool Handle_RandomTeleport (int clientID, const GameProtocol::CSRandomTeleportRequest& msg);

    // -----------------------------------------------------------------------
    // InnerPacket 핸들러 (GameDBService -> GameService 결과 수신)
    // -----------------------------------------------------------------------
    bool Handle_DB_LoginResponse(InnerPacket::SharedPtr pInner);

    // -----------------------------------------------------------------------
    // 유틸리티
    // -----------------------------------------------------------------------

    // FlatBuffers 프레임 패킷을 클라이언트에게 전송
    void _SendFB(int clientID, std::vector<uint8_t>& framed);

    // -----------------------------------------------------------------------
    // _SendError
    //   에러 발생 시 해당 클라이언트에게 SCIntegrationErrorNotification(500) 전송.
    //
    //   인자:
    //     clientID      : 수신 클라이언트 ID
    //     originMsgID   : 에러가 발생한 원래 요청 패킷의 EPacketProtocol 값
    //                     (예: EPacketProtocol_CS_PlayerMoveRequest = 201)
    //     err           : EErrorMsg 에러 코드
    //
    //   사용 예:
    //     return _SendError(clientID,
    //                       GameProtocol::EPacketProtocol_CS_PlayerMoveRequest,
    //                       GameProtocol::EErrorMsg_EF_FAIL_WRONG_REQ);
    // -----------------------------------------------------------------------
    bool _SendError(int clientID,
                    GameProtocol::EPacketProtocol originMsgID,
                    GameProtocol::EErrorMsg        err);
};
