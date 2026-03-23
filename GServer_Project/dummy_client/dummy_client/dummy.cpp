#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX   // Windows min/max 매크로가 FlatBuffers std::numeric_limits::max()와 충돌 방지

// FlatBuffers protocol (서버 프로토콜과 공유) — Windows 헤더보다 먼저 포함해야 매크로 충돌 방지
#include <flatbuffers/flatbuffers.h>
#include "../../GServer_Project/game_protocol_generated.h"

#include <WinSock2.h>
#include <winsock.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <array>

using namespace std;
using namespace chrono;

#pragma comment(lib, "ws2_32.lib")

// 패킷 ID 상수 (protocol.h와 동일)
constexpr int32_t CS_LOGIN_REQUEST               = 101;
constexpr int32_t SC_LOGIN_RESPONSE              = 104;
constexpr int32_t CS_PLAYER_MOVE_REQUEST         = 201;
constexpr int32_t SC_PLAYER_MOVE_RESPONSE        = 202;
constexpr int32_t SC_PLAYER_ATTACK_RESPONSE      = 206;
constexpr int32_t SC_PLAYER_CHATTING_RESPONSE    = 302;
constexpr int32_t SC_INTEGRATION_ERROR_NOTIFICATION = 500;
const short SERVER_PORT  = 4000;
const int   WORLD_WIDTH  = 2000;
const int   WORLD_HEIGHT = 2000;

extern HWND hWnd;

// -----------------------------------------------------------------------
// 상수
// -----------------------------------------------------------------------
constexpr int MAX_TEST       = 2000;           // 목표 동접자 수
constexpr int MAX_CLIENTS    = MAX_TEST * 2;   // 최대 소켓 수 (재접속 여유)
constexpr int INVALID_ID     = -1;
constexpr int RECV_BUF_SIZE  = 4096;           // IOCP recv 버퍼
constexpr int ACCUM_BUF_SIZE = RECV_BUF_SIZE * 4; // 누적 버퍼

HANDLE g_hiocp;

enum OPTYPE { OP_SEND, OP_RECV };

high_resolution_clock::time_point last_connect_time;

// -----------------------------------------------------------------------
// OverlappedEx
// -----------------------------------------------------------------------
struct OverlappedEx {
    WSAOVERLAPPED   over;
    WSABUF          wsabuf;
    unsigned char   IOCP_buf[RECV_BUF_SIZE];
    OPTYPE          event_type;
};

// -----------------------------------------------------------------------
// CLIENT: 더미 클라이언트 1개
// -----------------------------------------------------------------------
struct CLIENT {
    int             id;                     // 로컬 인덱스
    int             x, y;                   // 로컬 추적 좌표 (낙관적 업데이트)
    atomic_bool     connected;

    SOCKET          client_socket;
    OverlappedEx    recv_over;

    unsigned char   accum_buf[ACCUM_BUF_SIZE]; // 수신 누적 버퍼
    int             accum_size;                // 누적된 바이트 수

    int64_t         send_time_ms;  // 마지막 이동 패킷 전송 시각
    high_resolution_clock::time_point last_move_time;
};

array<CLIENT, MAX_CLIENTS> g_clients;
atomic_int  num_connections{ 0 };
atomic_int  client_to_close{ 0 };
atomic_int  active_clients{ 0 };
atomic_int  global_delay{ 0 };  // 이동 왕복 지연 (EMA, ms)

vector<thread*> worker_threads;
thread test_thread;

float point_cloud[MAX_TEST * 2];

// -----------------------------------------------------------------------
// error_display
// -----------------------------------------------------------------------
void error_display(const char* msg, int err_no)
{
    WCHAR* lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    cout << msg;
    wcout << lpMsgBuf << endl;
    LocalFree(lpMsgBuf);
}

// -----------------------------------------------------------------------
// DisconnectClient
// -----------------------------------------------------------------------
void DisconnectClient(int ci)
{
    bool expected = true;
    if (atomic_compare_exchange_strong(&g_clients[ci].connected, &expected, false)) {
        closesocket(g_clients[ci].client_socket);
        active_clients--;
    }
}

// -----------------------------------------------------------------------
// SendRaw: IOCP 비동기 전송
//   OverlappedEx를 new로 할당하여 IOCP에 올린 뒤 OP_SEND 완료 시 delete
// -----------------------------------------------------------------------
void SendRaw(int ci, const vector<uint8_t>& data)
{
    OverlappedEx* over = new OverlappedEx;
    over->event_type = OP_SEND;
    ZeroMemory(&over->over, sizeof(over->over));

    size_t size = min(data.size(), sizeof(over->IOCP_buf));
    memcpy(over->IOCP_buf, data.data(), size);
    over->wsabuf.buf = reinterpret_cast<CHAR*>(over->IOCP_buf);
    over->wsabuf.len = static_cast<ULONG>(size);

    int ret = WSASend(g_clients[ci].client_socket,
        &over->wsabuf, 1, NULL, 0, &over->over, NULL);
    if (ret != 0 && WSAGetLastError() != WSA_IO_PENDING) {
        delete over;
        DisconnectClient(ci);
    }
}

// -----------------------------------------------------------------------
// BuildFrame: CS 패킷 직렬화
//   포맷: [4바이트 msgId (LE)][4바이트 fb_size (LE)][fb_data]
// -----------------------------------------------------------------------
static vector<uint8_t> BuildFrame(int32_t msgId, flatbuffers::FlatBufferBuilder& fbb)
{
    const uint8_t* fb_buf = fbb.GetBufferPointer();
    uint32_t fb_size = fbb.GetSize();

    vector<uint8_t> frame(8 + fb_size);
    frame[0] = static_cast<uint8_t>(msgId & 0xFF);
    frame[1] = static_cast<uint8_t>((msgId >> 8)  & 0xFF);
    frame[2] = static_cast<uint8_t>((msgId >> 16) & 0xFF);
    frame[3] = static_cast<uint8_t>((msgId >> 24) & 0xFF);
    frame[4] = static_cast<uint8_t>(fb_size & 0xFF);
    frame[5] = static_cast<uint8_t>((fb_size >> 8)  & 0xFF);
    frame[6] = static_cast<uint8_t>((fb_size >> 16) & 0xFF);
    frame[7] = static_cast<uint8_t>((fb_size >> 24) & 0xFF);
    memcpy(frame.data() + 8, fb_buf, fb_size);
    return frame;
}

// -----------------------------------------------------------------------
// SendLogin: CSLogin (101)
// -----------------------------------------------------------------------
void SendLogin(int ci)
{
    flatbuffers::FlatBufferBuilder fbb;
    char name[32];
    sprintf_s(name, "%d", ci);
    auto name_str = fbb.CreateString(name);
    auto login = GameProtocol::CreateCSLogin(fbb, name_str);
    fbb.Finish(login);
    SendRaw(ci, BuildFrame(CS_LOGIN_REQUEST, fbb));
}

// -----------------------------------------------------------------------
// SendMove: CSPlayerMoveRequest (201)
//   전송 직전 로컬 좌표를 낙관적으로 업데이트 (시각화용)
// -----------------------------------------------------------------------
void SendMove(int ci, GameProtocol::Direction dir)
{
    // 낙관적 위치 업데이트
    switch (dir) {
    case GameProtocol::Direction_UP:    if (g_clients[ci].y > 0)              g_clients[ci].y--; break;
    case GameProtocol::Direction_DOWN:  if (g_clients[ci].y < WORLD_HEIGHT-1) g_clients[ci].y++; break;
    case GameProtocol::Direction_LEFT:  if (g_clients[ci].x > 0)              g_clients[ci].x--; break;
    case GameProtocol::Direction_RIGHT: if (g_clients[ci].x < WORLD_WIDTH-1)  g_clients[ci].x++; break;
    }

    g_clients[ci].send_time_ms = duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()).count();

    flatbuffers::FlatBufferBuilder fbb;
    auto move = GameProtocol::CreateCSPlayerMoveRequest(fbb, dir);
    fbb.Finish(move);
    SendRaw(ci, BuildFrame(CS_PLAYER_MOVE_REQUEST, fbb));
}

// -----------------------------------------------------------------------
// DispatchSCPacket: SC 패킷 처리
//   서버 → 클라이언트 포맷: [4 msgId][4 fb_size][fb_data]
//   fb_data / fb_size는 헤더를 제거한 뒤 전달됨
// -----------------------------------------------------------------------
void DispatchSCPacket(int ci, int32_t msgId,
                      const uint8_t* fb_data, uint32_t fb_size)
{
    switch (msgId)
    {
    case SC_LOGIN_RESPONSE:  // 104: 서버는 SCPlayerMoveResponse 구조체 재사용
    {
        flatbuffers::Verifier v(fb_data, fb_size);
        if (!v.VerifyBuffer<GameProtocol::SCPlayerMoveResponse>()) break;
        g_clients[ci].connected = true;
        active_clients++;
        break;
    }

    case SC_PLAYER_MOVE_RESPONSE:  // 202
    {
        int64_t now_ms = duration_cast<milliseconds>(
            high_resolution_clock::now().time_since_epoch()).count();
        int64_t d_ms = now_ms - g_clients[ci].send_time_ms;
        if (d_ms > 0 && d_ms < 10000) {
            int cur = global_delay.load();
            if (cur < (int)d_ms) global_delay++;
            else if (cur > (int)d_ms) global_delay--;
        }
        break;
    }

    case SC_PLAYER_ATTACK_RESPONSE:    // 206  (스트레스 테스트에서 공격 미사용)
    case SC_PLAYER_CHATTING_RESPONSE:  // 302  (NPC 채팅 수신 시 무시)
        break;

    case SC_INTEGRATION_ERROR_NOTIFICATION:  // 500: 로그인 실패 등
        DisconnectClient(ci);
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------
// ProcessRecvData: 수신 데이터 누적 및 패킷 경계 파싱
//   SC 포맷: [4 msgId][4 fb_size][fb_data] (8바이트 헤더)
// -----------------------------------------------------------------------
void ProcessRecvData(int ci, const unsigned char* data, int data_size)
{
    CLIENT& cl = g_clients[ci];

    if (cl.accum_size + data_size > ACCUM_BUF_SIZE) {
        DisconnectClient(ci);
        return;
    }
    memcpy(cl.accum_buf + cl.accum_size, data, data_size);
    cl.accum_size += data_size;

    unsigned char* buf       = cl.accum_buf;
    int            remaining = cl.accum_size;

    while (remaining >= 8) {
        int32_t  msgId   = static_cast<int32_t>(
            static_cast<uint32_t>(buf[0])        |
            (static_cast<uint32_t>(buf[1]) << 8)  |
            (static_cast<uint32_t>(buf[2]) << 16) |
            (static_cast<uint32_t>(buf[3]) << 24));
        uint32_t fb_size =
            static_cast<uint32_t>(buf[4])        |
            (static_cast<uint32_t>(buf[5]) << 8)  |
            (static_cast<uint32_t>(buf[6]) << 16) |
            (static_cast<uint32_t>(buf[7]) << 24);
        uint32_t full_size = 8 + fb_size;

        if (remaining < static_cast<int>(full_size)) break;

        DispatchSCPacket(ci, msgId, buf + 8, fb_size);

        buf       += full_size;
        remaining -= static_cast<int>(full_size);
    }

    // 처리되지 않은 데이터를 버퍼 앞으로 이동
    if (remaining > 0 && buf != cl.accum_buf)
        memmove(cl.accum_buf, buf, remaining);
    cl.accum_size = remaining;
}

// -----------------------------------------------------------------------
// Worker_Thread: IOCP 이벤트 처리
// -----------------------------------------------------------------------
void Worker_Thread()
{
    while (true) {
        DWORD         io_size;
        unsigned long long ci;
        OverlappedEx* over;

        BOOL ret = GetQueuedCompletionStatus(g_hiocp, &io_size,
            &ci, reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);

        int client_id = static_cast<int>(ci);

        if (FALSE == ret) {
            DisconnectClient(client_id);
            if (over && over->event_type == OP_SEND) delete over;
            continue;
        }
        if (io_size == 0) {
            DisconnectClient(client_id);
            if (over && over->event_type == OP_SEND) delete over;
            continue;
        }

        if (over->event_type == OP_RECV) {
            ProcessRecvData(client_id, over->IOCP_buf, static_cast<int>(io_size));

            // recv 재등록
            DWORD recv_flag = 0;
            WSARecv(g_clients[client_id].client_socket,
                &g_clients[client_id].recv_over.wsabuf, 1,
                NULL, &recv_flag,
                &g_clients[client_id].recv_over.over, NULL);
        }
        else if (over->event_type == OP_SEND) {
            delete over;
        }
    }
}

// -----------------------------------------------------------------------
// Adjust_Number_Of_Client: 서버 지연에 따라 접속자 수 자동 조절
//
//   delay < 100ms  → 50ms 간격으로 클라이언트 1개 추가
//   delay 100~150ms → 접속 속도 10배 늦춤 (대기)
//   delay > 150ms  → 클라이언트 1개씩 해제 (500ms 간격)
// -----------------------------------------------------------------------
constexpr int DELAY_LIMIT  = 100;
constexpr int DELAY_LIMIT2 = 150;
constexpr int ACCEPT_DELAY = 50;

void Adjust_Number_Of_Client()
{
    static int  delay_multiplier = 1;
    static int  max_limit        = INT_MAX;
    static bool increasing       = true;

    if (active_clients >= MAX_TEST)   return;
    if (num_connections >= MAX_CLIENTS) return;

    auto elapsed = high_resolution_clock::now() - last_connect_time;
    if (ACCEPT_DELAY * delay_multiplier >
        (int)duration_cast<milliseconds>(elapsed).count()) return;

    int t_delay = global_delay.load();

    if (t_delay > DELAY_LIMIT2) {
        if (increasing) { max_limit = active_clients; increasing = false; }
        if (active_clients < 100) return;
        if (ACCEPT_DELAY * 10 > (int)duration_cast<milliseconds>(elapsed).count()) return;
        last_connect_time = high_resolution_clock::now();
        DisconnectClient(client_to_close++);
        return;
    }
    else if (t_delay > DELAY_LIMIT) {
        delay_multiplier = 10;
        return;
    }

    if (max_limit - (max_limit / 20) < active_clients) return;

    increasing = true;
    delay_multiplier = 1;
    last_connect_time = high_resolution_clock::now();

    int idx = num_connections;

    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
        NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) return;

    SOCKADDR_IN addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (WSAConnect(s, (sockaddr*)&addr, sizeof(addr),
                   NULL, NULL, NULL, NULL) != 0
        && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        int err = WSAGetLastError();
        printf("WSAConnect failed: %d\n", err);
        closesocket(s);
        return;
    }

    CLIENT& cl       = g_clients[idx];
    cl.client_socket = s;
    cl.id            = idx;
    cl.x             = rand() % WORLD_WIDTH;
    cl.y             = rand() % WORLD_HEIGHT;
    cl.accum_size    = 0;
    cl.connected     = false;
    cl.send_time_ms  = 0;

    ZeroMemory(&cl.recv_over, sizeof(cl.recv_over));
    cl.recv_over.event_type    = OP_RECV;
    cl.recv_over.wsabuf.buf    = reinterpret_cast<CHAR*>(cl.recv_over.IOCP_buf);
    cl.recv_over.wsabuf.len    = sizeof(cl.recv_over.IOCP_buf);

    CreateIoCompletionPort(reinterpret_cast<HANDLE>(s), g_hiocp, idx, 0);

    DWORD recv_flag = 0;
    int ret = WSARecv(s, &cl.recv_over.wsabuf, 1,
        NULL, &recv_flag, &cl.recv_over.over, NULL);
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        closesocket(s);
        return;
    }

    SendLogin(idx);
    num_connections++;
}

// -----------------------------------------------------------------------
// Test_Thread: 접속 수 조절 + 랜덤 이동 패킷 전송
// -----------------------------------------------------------------------
void Test_Thread()
{
    while (true) {
        Adjust_Number_Of_Client();

        int n = num_connections.load();
        for (int i = 0; i < n; ++i) {
            if (!g_clients[i].connected) continue;
            if (g_clients[i].last_move_time + 1s > high_resolution_clock::now()) continue;
            g_clients[i].last_move_time = high_resolution_clock::now();
            SendMove(i, static_cast<GameProtocol::Direction>(rand() % 4));
        }

        this_thread::sleep_for(10ms);
    }
}

// -----------------------------------------------------------------------
// InitializeNetwork
// -----------------------------------------------------------------------
void InitializeNetwork()
{
    for (auto& cl : g_clients) {
        cl.connected  = false;
        cl.id         = INVALID_ID;
        cl.accum_size = 0;
    }

    num_connections = 0;
    client_to_close = 0;
    active_clients  = 0;
    global_delay    = 0;
    last_connect_time = high_resolution_clock::now();

    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

    for (int i = 0; i < 6; ++i)
        worker_threads.push_back(new thread{ Worker_Thread });

    test_thread = thread{ Test_Thread };
}

// -----------------------------------------------------------------------
// GetPointCloud: 연결된 클라이언트의 좌표를 OpenGL 점구름으로 내보냄
// -----------------------------------------------------------------------
void GetPointCloud(int* size, float** points)
{
    int index = 0;
    for (int i = 0; i < num_connections && index < MAX_TEST; ++i) {
        if (!g_clients[i].connected) continue;
        point_cloud[index * 2]     = static_cast<float>(g_clients[i].x);
        point_cloud[index * 2 + 1] = static_cast<float>(g_clients[i].y);
        index++;
    }
    *size   = index;
    *points = point_cloud;
}
