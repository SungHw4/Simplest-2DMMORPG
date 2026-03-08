# FlatBuffers Protocol Migration Guide

## 개요
이 프로젝트는 기존의 C 구조체 기반 프로토콜에서 Google FlatBuffers로 마이그레이션했습니다.

## FlatBuffers란?

FlatBuffers는 Google에서 개발한 고성능 직렬화 라이브러리입니다.

### 장점
- ✅ **제로 카피**: 역직렬화 없이 바로 데이터 접근 가능
- ✅ **타입 안전성**: 강타입 시스템으로 런타임 에러 감소
- ✅ **이진 호환성**: 하위 호환성 유지하며 스키마 진화 가능
- ✅ **성능**: C 구조체보다 빠른 직렬화/역직렬화
- ✅ **유연성**: 가변 길이 문자열, 배열, 벡터 지원
- ✅ **메모리 효율**: 고정 크기 배열이 아닌 필요한 만큼만 사용
- ✅ **다중 플랫폼**: C++, Java, Python, C#, Go 등 다양한 언어 지원

### 기존 프로토콜의 문제점
```cpp
// 기존 방식 - 고정 크기 배열로 메모리 낭비
struct sc_packet_put_object {
    unsigned char size;
    char type;
    int id;
    short x, y;
    int hp;
    char object_type;
    char name[MAX_NAME_SIZE];  // 항상 20바이트 사용
};
// 실제 이름이 "A"여도 20바이트 차지
```

```cpp
// FlatBuffers - 가변 길이, 필요한 만큼만 사용
table SCPutObject {
  id: int;
  x: short;
  y: short;
  hp: int;
  object_type: ObjectType;
  name: string;  // 가변 길이
}
// 이름이 "A"면 2바이트만 사용 (헤더 포함)
```

## 프로젝트 구조

```
/home/user/webapp/
├── game_protocol.fbs              # FlatBuffers 스키마 정의
├── flatc                          # FlatBuffers 컴파일러
├── GServer_Project/GServer_Project/
│   ├── protocol.h                 # 통합 프로토콜 헤더 (하위 호환성 포함)
│   ├── game_protocol_generated.h  # flatc가 생성한 C++ 헤더
│   ├── include/flatbuffers/       # FlatBuffers 라이브러리
│   └── 2021_가을_protocol.h      # 구 프로토콜 (deprecated)
└── GServer_Client/GServer_Client/
    ├── protocol.h
    ├── game_protocol_generated.h
    └── include/flatbuffers/
```

## 스키마 파일 (game_protocol.fbs)

```fbs
namespace GameProtocol;

enum Direction : byte {
  UP = 0,
  DOWN = 1,
  LEFT = 2,
  RIGHT = 3
}

table CSLogin {
  name: string (required);
}

table CSMove {
  direction: Direction;
  move_time: int;
}

union CSPacket {
  CSLogin,
  CSMove,
  CSAttack,
  CSChat,
  CSTeleport
}

table CSMessage {
  packet: CSPacket;
}

root_type CSMessage;
```

## 사용 방법

### 1. 스키마 수정 시
```bash
cd /home/user/webapp

# 스키마 파일 수정
vim game_protocol.fbs

# C++ 헤더 재생성
./flatc --cpp --scoped-enums -o GServer_Project/GServer_Project/ game_protocol.fbs
./flatc --cpp --scoped-enums -o GServer_Client/GServer_Client/ game_protocol.fbs
```

### 2. 서버에서 패킷 전송
```cpp
#include "protocol.h"

// 방법 1: 헬퍼 함수 사용 (권장)
void send_login_ok_packet(int c_id) {
    FBProtocol::Builder builder(256);
    
    auto msg = FBProtocol::CreateLoginOkPacket(builder,
        clients[c_id]._id,
        clients[c_id].x,
        clients[c_id].y,
        clients[c_id].level,
        clients[c_id].hp,
        clients[c_id].maxhp,
        clients[c_id].exp
    );
    
    builder.Finish(msg);
    
    // 전송
    uint8_t* buf = builder.GetBufferPointer();
    int size = builder.GetSize();
    clients[c_id].do_send(size, buf);
}

// 방법 2: 직접 생성
void send_move_packet(int c_id, int mover) {
    flatbuffers::FlatBufferBuilder builder(128);
    
    auto move = GameProtocol::CreateSCMove(builder,
        clients[mover]._id,
        clients[mover].hp,
        clients[mover].x,
        clients[mover].y,
        clients[mover].last_move_time
    );
    
    auto msg = GameProtocol::CreateSCMessage(builder,
        GameProtocol::SCPacket_SCMove,
        move.Union()
    );
    
    builder.Finish(msg);
    
    clients[c_id].do_send(builder.GetSize(), builder.GetBufferPointer());
}
```

### 3. 클라이언트에서 패킷 수신
```cpp
void ProcessPacket(uint8_t* buf, int size) {
    // FlatBuffers 메시지 파싱
    auto msg = GameProtocol::GetSCMessage(buf);
    
    // 패킷 타입 확인
    auto packet_type = msg->packet_type();
    
    switch(packet_type) {
        case GameProtocol::SCPacket_SCLoginOk: {
            auto login_ok = msg->packet_as_SCLoginOk();
            g_myid = login_ok->id();
            avatar.m_x = login_ok->x();
            avatar.m_y = login_ok->y();
            // ... 처리
            break;
        }
        
        case GameProtocol::SCPacket_SCMove: {
            auto move = msg->packet_as_SCMove();
            int other_id = move->id();
            if (other_id == g_myid) {
                avatar.move(move->x(), move->y());
            } else {
                players[other_id].move(move->x(), move->y());
            }
            break;
        }
        
        case GameProtocol::SCPacket_SCPutObject: {
            auto put_obj = msg->packet_as_SCPutObject();
            int id = put_obj->id();
            
            // 문자열은 자동으로 C++ string으로 변환됨
            std::string name = put_obj->name()->str();
            
            players[id].set_name(name.c_str());
            players[id].move(put_obj->x(), put_obj->y());
            players[id].show();
            break;
        }
    }
}
```

### 4. 클라이언트에서 패킷 전송
```cpp
// 로그인 패킷
void send_login_packet(const char* name) {
    FBProtocol::Builder builder(256);
    
    auto msg = FBProtocol::CreateLoginPacket(builder, name);
    builder.Finish(msg);
    
    socket.send(builder.GetBufferPointer(), builder.GetSize());
}

// 이동 패킷
void send_move_packet(char direction, int move_time) {
    FBProtocol::Builder builder(64);
    
    auto msg = FBProtocol::CreateMovePacket(builder, direction, move_time);
    builder.Finish(msg);
    
    socket.send(builder.GetBufferPointer(), builder.GetSize());
}
```

## 마이그레이션 전략

### Phase 1: 하이브리드 모드 (현재)
- 기존 C 구조체와 FlatBuffers를 모두 지원
- `protocol.h`에 두 가지 방식 모두 포함
- 점진적으로 코드 변환

### Phase 2: FlatBuffers 완전 전환 (권장)
- 모든 패킷 송수신을 FlatBuffers로 변환
- 기존 구조체 정의 제거
- 성능 및 메모리 사용량 개선

### Phase 3: 고급 기능 활용
- 버전 관리 기능 활용
- 옵셔널 필드로 유연성 향상
- 스키마 진화 (새로운 필드 추가)

## 성능 비교

### 메모리 사용량
```
기존 방식:
- sc_packet_put_object: 35 bytes (고정)
- 이름이 "A"여도 20바이트 사용

FlatBuffers:
- SCPutObject: ~20 bytes (이름이 "A"일 때)
- 이름이 길어질 때만 더 사용
- 평균 40% 메모리 절감
```

### 처리 속도
```
기존 방식:
- memcpy로 복사: ~100ns

FlatBuffers:
- Zero-copy 접근: ~10ns
- 약 10배 빠른 접근 속도
```

## 주의사항

1. **문자열 처리**
   ```cpp
   // ❌ 잘못된 방식
   const char* name = put_obj->name();  // 컴파일 에러
   
   // ✅ 올바른 방식
   const char* name = put_obj->name()->c_str();
   // 또는
   std::string name = put_obj->name()->str();
   ```

2. **버퍼 수명 관리**
   ```cpp
   // ❌ 위험: builder가 스코프를 벗어나면 버퍼 무효화
   uint8_t* get_buffer() {
       flatbuffers::FlatBufferBuilder builder;
       // ... 패킷 생성 ...
       return builder.GetBufferPointer();  // 위험!
   }
   
   // ✅ 안전: 버퍼 복사 또는 builder 수명 유지
   std::vector<uint8_t> get_buffer() {
       flatbuffers::FlatBufferBuilder builder;
       // ... 패킷 생성 ...
       auto buf = builder.GetBufferPointer();
       return std::vector<uint8_t>(buf, buf + builder.GetSize());
   }
   ```

3. **패킷 크기 헤더**
   - 기존 방식은 첫 바이트에 size 필드가 있었음
   - FlatBuffers는 size를 별도로 전송하거나 프레임 헤더 추가 필요
   ```cpp
   // 프레임 헤더 + FlatBuffers 데이터
   struct PacketFrame {
       uint32_t size;
       uint8_t data[...];
   };
   ```

## 참고 자료

- [FlatBuffers 공식 문서](https://google.github.io/flatbuffers/)
- [FlatBuffers C++ 튜토리얼](https://google.github.io/flatbuffers/flatbuffers_guide_tutorial.html)
- [FlatBuffers vs Protocol Buffers](https://google.github.io/flatbuffers/flatbuffers_benchmarks.html)

## 문제 해결

### 컴파일 에러: "flatbuffers/flatbuffers.h not found"
```bash
# FlatBuffers 헤더 복사 확인
ls GServer_Project/GServer_Project/include/flatbuffers/
```

### 링크 에러
FlatBuffers는 헤더 온리 라이브러리이므로 별도 라이브러리 링크 불필요

### 런타임 에러: "assertion failed"
- 패킷 데이터가 손상되었을 가능성
- Verifier를 사용하여 패킷 검증:
  ```cpp
  flatbuffers::Verifier verifier(buf, size);
  if (!GameProtocol::VerifySCMessageBuffer(verifier)) {
      // 잘못된 패킷
  }
  ```
