# Protocol Build System Guide

## 개요
Protocol 프로젝트는 FlatBuffers 스키마를 자동으로 컴파일하여 C++ 헤더를 생성하는 Visual Studio 프로젝트입니다.

## 프로젝트 구조

```
GServer_Project/                   GServer_Client/
├── Protocol/                      ├── Protocol/
│   ├── protocol.fbs              │   ├── protocol.fbs
│   ├── flatc.exe                 │   ├── flatc.exe
│   ├── Protocol.vcxproj          │   ├── Protocol.vcxproj
│   └── Generated/                │   └── Generated/
│       └── protocol_generated.h  │       └── protocol_generated.h
├── GServer_Project/              ├── GServer_Client/
│   ├── protocol_generated.h (복사본)  │   ├── protocol_generated.h (복사본)
│   └── ...                       │   └── ...
└── GServer_Project.sln           └── GServer_Client.sln
```

## 빌드 순서

### Visual Studio에서 솔루션 빌드 시:
1. **Protocol 프로젝트가 먼저 빌드됨** (의존성 설정)
2. `protocol.fbs` 파일이 변경되었는지 확인
3. 변경되었으면 `flatc.exe` 실행하여 `protocol_generated.h` 생성
4. 생성된 헤더를 `GServer_Project/` 또는 `GServer_Client/`로 복사
5. 이후 메인 프로젝트(GServer_Project 또는 GServer_Client) 빌드

## 사용 방법

### 1. 프로토콜 스키마 수정
```bash
# 서버 또는 클라이언트 Protocol 디렉토리에서
vim GServer_Project/Protocol/protocol.fbs
```

```fbs
// 예: 새로운 패킷 추가
table CSNewFeature {
  feature_id: int;
  feature_data: string;
}

union CSPacket {
  CSLogin,
  CSMove,
  CSAttack,
  CSChat,
  CSTeleport,
  CSNewFeature  // 추가
}
```

### 2. 솔루션 빌드
Visual Studio에서:
- **솔루션 빌드** (Ctrl+Shift+B) 또는
- **솔루션 다시 빌드** (Ctrl+Alt+F7)

빌드 출력 예시:
```
1>------ Build started: Project: Protocol, Configuration: Debug x64 ------
1>Generating FlatBuffers protocol headers...
1>Copying generated headers to server project...
1>Protocol generation complete!
1>  Protocol.vcxproj -> D:\GServer_Project\x64\Debug\
2>------ Build started: Project: GServer_Project, Configuration: Debug x64 ------
...
```

### 3. 생성된 헤더 사용
프로토콜이 자동으로 재생성되므로 코드에서 바로 사용 가능:

```cpp
#include "protocol_generated.h"

// 새 패킷 사용
flatbuffers::FlatBufferBuilder builder;
auto feature_data = builder.CreateString("test data");
auto new_feature = GameProtocol::CreateCSNewFeature(builder, 
    123, feature_data);
```

## 프로젝트 설정 세부사항

### Protocol 프로젝트 타입
- **ConfigurationType**: `Utility`
- 실행 파일이나 라이브러리를 생성하지 않음
- 커스텀 빌드 스텝만 실행

### 커스텀 빌드 규칙

#### 입력:
- `protocol.fbs` - FlatBuffers 스키마 파일

#### 명령어:
```batch
echo Generating FlatBuffers protocol headers...
$(ProjectDir)flatc.exe --cpp --scoped-enums -o $(ProjectDir)Generated $(ProjectDir)protocol.fbs
echo Copying generated headers to server project...
xcopy /Y /Q "$(ProjectDir)Generated\*.h" "$(SolutionDir)GServer_Project\"
echo Protocol generation complete!
```

#### 출력:
- `$(ProjectDir)Generated\protocol_generated.h`

### 의존성 설정

#### GServer_Project.sln:
```xml
Project("{...}") = "GServer_Project", "GServer_Project\GServer_Project.vcxproj", "{...}"
    ProjectSection(ProjectDependencies) = postProject
        {B8E9A4F2-1D3C-4A5E-8F7B-9C2D1E3F4A5B} = {B8E9A4F2-1D3C-4A5E-8F7B-9C2D1E3F4A5B}
    EndProjectSection
EndProject
```

이렇게 하면 Protocol 프로젝트가 항상 먼저 빌드됨

## 트러블슈팅

### 문제 1: "flatc.exe가 없습니다"
**증상**: 빌드 시 flatc.exe를 찾을 수 없다는 오류

**해결**:
```bash
cd GServer_Project/Protocol
# flatc.exe가 있는지 확인
ls -la flatc.exe
# 없으면 복사
cp ../../flatc .
```

### 문제 2: "protocol_generated.h가 최신이 아닙니다"
**증상**: 스키마를 변경했는데 반영이 안 됨

**해결**:
1. Visual Studio에서 **솔루션 정리** (Clean Solution)
2. **솔루션 다시 빌드** (Rebuild Solution)
3. 또는 수동으로 생성:
```bash
cd GServer_Project/Protocol
./flatc.exe --cpp --scoped-enums -o Generated protocol.fbs
cp Generated/*.h ../GServer_Project/
```

### 문제 3: "프로젝트 빌드 순서가 잘못됨"
**증상**: Protocol이 나중에 빌드되어 헤더를 찾을 수 없음

**해결**:
Visual Studio에서:
1. 솔루션 우클릭 → **프로젝트 의존성** (Project Dependencies)
2. GServer_Project 선택
3. Protocol 체크박스 확인
4. 또는 솔루션 파일에서 ProjectDependencies 섹션 확인

### 문제 4: "경로가 너무 깁니다"
**증상**: Windows 경로 길이 제한 (260자) 초과

**해결**:
- 프로젝트를 더 짧은 경로로 이동
- 예: `D:\GServer\` 대신 `C:\Dev\`

## 고급 사용법

### 다른 플랫폼용 헤더 생성

#### Python 클라이언트용:
```bash
cd GServer_Project/Protocol
./flatc.exe --python -o Generated_Python protocol.fbs
```

#### C# 클라이언트용:
```bash
./flatc.exe --csharp -o Generated_CSharp protocol.fbs
```

#### JavaScript 웹 클라이언트용:
```bash
./flatc.exe --ts -o Generated_TypeScript protocol.fbs
```

### 여러 스키마 파일 관리

#### 스키마 분리:
```
Protocol/
├── protocol.fbs          (메인, includes로 다른 파일 참조)
├── common_types.fbs      (공통 타입 정의)
├── client_packets.fbs    (클라이언트→서버 패킷)
└── server_packets.fbs    (서버→클라이언트 패킷)
```

#### protocol.fbs:
```fbs
include "common_types.fbs";
include "client_packets.fbs";
include "server_packets.fbs";

namespace GameProtocol;

// 메인 정의
union CSPacket {
  CSLogin,
  CSMove,
  // ...
}
```

#### 빌드 명령 업데이트:
```batch
flatc.exe --cpp --scoped-enums -o Generated ^
  common_types.fbs ^
  client_packets.fbs ^
  server_packets.fbs ^
  protocol.fbs
```

## CI/CD 통합

### GitHub Actions 예시:
```yaml
name: Build Protocol
on: [push, pull_request]
jobs:
  build-protocol:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Generate Protocol Headers
        run: |
          cd GServer_Project/Protocol
          ./flatc.exe --cpp --scoped-enums -o Generated protocol.fbs
      
      - name: Upload Generated Headers
        uses: actions/upload-artifact@v2
        with:
          name: protocol-headers
          path: GServer_Project/Protocol/Generated/*.h
```

## 참고 자료

- [FlatBuffers 공식 문서](https://google.github.io/flatbuffers/)
- [Visual Studio 커스텀 빌드 도구](https://docs.microsoft.com/en-us/cpp/build/reference/specifying-custom-build-tools)
- [MSBuild 커스텀 타겟](https://docs.microsoft.com/en-us/visualstudio/msbuild/msbuild-targets)

## 요약

✅ **Protocol 프로젝트를 빌드하면 자동으로 프로토콜 헤더가 생성됩니다**
✅ **의존성 설정으로 항상 Protocol이 먼저 빌드됩니다**
✅ **스키마 변경 시 솔루션 빌드만 하면 자동 반영됩니다**
✅ **수동 개입 없이 자동화된 워크플로우**

---

**프로토콜 변경 워크플로우:**
1. `protocol.fbs` 수정
2. Visual Studio에서 F7 (빌드)
3. 끝! (자동으로 헤더 생성 및 복사)
