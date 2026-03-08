# Protocol Build System Guide

## 개요
Protocol 프로젝트는 FlatBuffers 스키마(`protocol.fbs`)를 자동으로 컴파일하여 C++ 헤더(`protocol_generated.h`)를 생성하는 Visual Studio Utility 프로젝트입니다. 솔루션 빌드 시 자동으로 실행되며, GServer_Project 및 GServer_Client에서 사용할 프로토콜 헤더를 생성합니다.

## 주요 특징
- ✅ **자동 빌드**: 솔루션 빌드 시 Protocol 프로젝트가 자동으로 먼저 실행됩니다
- ✅ **의존성 관리**: 프로젝트 의존성 설정으로 빌드 순서 보장
- ✅ **증분 빌드**: `.fbs` 파일 변경 시에만 재생성
- ✅ **vcpkg 통합**: vcpkg를 통한 FlatBuffers 라이브러리 자동 관리
- ✅ **경고 제거**: `--no-warnings` 옵션으로 네이밍 컨벤션 경고 무시
- ✅ **Visual Studio 통합**: 솔루션 탐색기에서 프로토콜 스키마 관리

## 프로젝트 구조

```
GServer_Project/                        GServer_Client/
├── GServer_Project.sln                ├── GServer_Client.sln
│   (Protocol → GServer_Project)       │   (Protocol → GServer_Client)
├── Protocol/                          ├── Protocol/
│   ├── protocol.fbs                   │   ├── protocol.fbs
│   ├── flatc.exe (Windows 전용)       │   ├── flatc.exe (Windows 전용)
│   ├── Protocol.vcxproj               │   ├── Protocol.vcxproj
│   ├── Protocol.vcxproj.filters       │   ├── Protocol.vcxproj.filters
│   └── protocol_generated.h (빌드시)  │   └── protocol_generated.h (빌드시)
└── GServer_Project/                   └── GServer_Client/
    └── ...                            └── ...
```

### 파일 설명
- **protocol.fbs**: FlatBuffers 스키마 정의 파일 (수동 편집)
- **flatc.exe**: FlatBuffers 컴파일러 실행 파일 (Windows 전용)
- **Protocol.vcxproj**: Visual Studio 프로젝트 파일 (CustomBuild 규칙 포함)
- **Protocol.vcxproj.filters**: 솔루션 탐색기 필터 정의
- **protocol_generated.h**: 자동 생성된 C++ 헤더 (빌드 시 Protocol 폴더에 직접 생성)

## 필수 요구사항

### 1. vcpkg 설치 및 FlatBuffers 라이브러리
FlatBuffers를 사용하려면 **헤더 파일**이 필요합니다. vcpkg로 설치하는 것을 강력히 권장합니다.

#### vcpkg 설치 방법:
```powershell
# vcpkg 클론 (원하는 위치에)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# vcpkg 부트스트랩
.\bootstrap-vcpkg.bat

# FlatBuffers 설치 (x64-windows)
.\vcpkg install flatbuffers:x64-windows

# Visual Studio 통합 (자동으로 include 경로 설정)
.\vcpkg integrate install
```

#### 통합 후:
```
Applied user-wide integration for this vcpkg root.
CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake"

All MSBuild C++ projects can now #include any installed libraries. Linking will be handled automatically.
```

Visual Studio를 재시작하면 자동으로 FlatBuffers 헤더를 찾을 수 있습니다.

### 2. flatc.exe 컴파일러 (Protocol 프로젝트용)
vcpkg를 사용하면 flatc.exe도 함께 설치됩니다:
```
vcpkg 설치 경로\installed\x64-windows\tools\flatbuffers\flatc.exe
```

이 flatc.exe를 Protocol 폴더에 복사하거나, Protocol.vcxproj에서 vcpkg 경로를 직접 참조할 수 있습니다.

## 빌드 순서

### Visual Studio에서 솔루션 빌드 시:
1. **Protocol 프로젝트가 먼저 빌드됨** (의존성 설정)
2. `protocol.fbs` 파일이 변경되었는지 확인
3. 변경되었으면 `flatc.exe` 실행하여 `protocol_generated.h` 생성
4. 생성된 헤더가 Protocol 폴더에 직접 저장됨
5. 이후 메인 프로젝트(GServer_Project 또는 GServer_Client) 빌드

## 사용 방법

### 1. 프로토콜 스키마 수정
```bash
# 서버 또는 클라이언트 Protocol 디렉토리에서
vim GServer_Project/Protocol/protocol.fbs
```

```fbs
// 예: 새로운 패킷 추가
namespace GameProtocol;

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
1>Generating FlatBuffers header from protocol.fbs
1>  Protocol.vcxproj -> D:\GServer_Project\x64\Debug\
2>------ Build started: Project: GServer_Project, Configuration: Debug x64 ------
...
```

### 3. 생성된 헤더 사용

#### 메인 프로젝트 Include 경로 설정:
프로젝트 속성 → C/C++ → General → Additional Include Directories:

```
$(SolutionDir)Protocol
```

GServer_Project.sln과 Protocol 폴더가 같은 레벨에 있다면 위 설정으로 충분합니다.

#### 코드에서 사용:
```cpp
#include "protocol_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <iostream>

int main() {
    flatbuffers::FlatBufferBuilder builder(1024);
    
    // 새 패킷 사용
    auto feature_data = builder.CreateString("test data");
    auto new_feature = GameProtocol::CreateCSNewFeature(builder, 
        123, feature_data);
    
    builder.Finish(new_feature);
    
    std::cout << "Serialized size: " << builder.GetSize() << " bytes" << std::endl;
    
    return 0;
}
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
"$(ProjectDir)flatc.exe" --cpp --no-warnings -o "$(ProjectDir)" "%(FullPath)"
```

#### 출력:
- `$(ProjectDir)protocol_generated.h`

#### 주요 변경사항 (기존 대비):
- ✅ `--no-warnings` 추가: snake_case 네이밍 경고 제거
- ✅ `-o "$(ProjectDir)"`: Protocol 폴더에 직접 생성 (Generated 폴더 불필요)
- ✅ `"%(FullPath)"`: 파일 경로를 매크로로 처리
- ✅ `AdditionalInputs`: flatc.exe 변경 시에도 재빌드

### 의존성 설정

#### GServer_Project.sln:
```xml
Project("{...}") = "GServer_Project", "GServer_Project\GServer_Project.vcxproj", "{...}"
    ProjectSection(ProjectDependencies) = postProject
        {B8E9A4F2-1D3C-4A5E-8F7B-9C2D1E3F4A5B} = {B8E9A4F2-1D3C-4A5E-8F7B-9C2D1E3F4A5B}
    EndProjectSection
EndProject
```

이렇게 하면 Protocol 프로젝트가 항상 먼저 빌드됩니다.

## 트러블슈팅

### 문제 1: "솔루션 탐색기에 Protocol 프로젝트가 안 보입니다"
**증상**: Visual Studio에서 Protocol 프로젝트가 표시되지 않음

**원인**: 
- 솔루션 파일(.sln)에 Protocol 프로젝트가 등록되지 않음
- 프로젝트 파일(.vcxproj)에 오류가 있음

**해결**:
1. Visual Studio에서 솔루션 닫기
2. 솔루션 파일 확인:
   ```
   # GServer_Project.sln에 다음 내용이 있는지 확인
   Project("{...}") = "Protocol", "Protocol\Protocol.vcxproj", "{B8E9A4F2-...}"
   EndProject
   ```
3. Visual Studio에서 솔루션 다시 열기
4. 여전히 안 보이면 수동 추가:
   - 솔루션 우클릭 → 추가 → 기존 프로젝트
   - `Protocol/Protocol.vcxproj` 선택

### 문제 2: "duplicate project item: protocol.fbs" 오류
**증상**: Protocol 프로젝트 로드 시 오류 발생
```
Protocol.vcxproj : error : protocol.fbs included as both 'CustomBuild' and 'None'
```

**원인**: .vcxproj 파일에서 `protocol.fbs`가 `<None>`과 `<CustomBuild>`에 중복 포함됨

**해결**:
Protocol.vcxproj에서 `<None Include="protocol.fbs" />` 항목 제거
```xml
<!-- 잘못된 구조 -->
<ItemGroup>
  <None Include="protocol.fbs" />
</ItemGroup>
<ItemGroup>
  <CustomBuild Include="protocol.fbs">
    ...
  </CustomBuild>
</ItemGroup>

<!-- 올바른 구조 -->
<ItemGroup>
  <CustomBuild Include="protocol.fbs">
    ...
  </CustomBuild>
</ItemGroup>
```

### 문제 3: "flatbuffers/flatbuffers.h를 열 수 없습니다"
**증상**: 메인 프로젝트에서 FlatBuffers 헤더를 찾을 수 없음

**원인**: 
- vcpkg로 FlatBuffers 라이브러리를 설치하지 않음
- vcpkg integrate가 적용되지 않음

**해결**:
```powershell
# vcpkg 설치 확인
vcpkg list flatbuffers

# 없으면 설치
vcpkg install flatbuffers:x64-windows

# Visual Studio 통합
vcpkg integrate install

# Visual Studio 재시작
```

### 문제 4: "Non-compatible flatbuffers version included" 정적 어설션 실패
**증상**: protocol_generated.h에서 버전 불일치 오류

**원인**: flatc.exe 버전과 vcpkg 라이브러리 버전이 다름

**해결**:
vcpkg의 flatc.exe를 사용하도록 변경:
```xml
<!-- Protocol.vcxproj 수정 -->
<Command>
  "E:\vcpkg\installed\x64-windows\tools\flatbuffers\flatc.exe" --cpp --no-warnings -o "$(ProjectDir)" "%(FullPath)"
</Command>
```

또는 Protocol 폴더의 flatc.exe를 vcpkg 버전으로 교체:
```powershell
copy "E:\vcpkg\installed\x64-windows\tools\flatbuffers\flatc.exe" "Protocol\flatc.exe"
```

### 문제 5: "#include \"protocol_generated.h\"를 찾을 수 없습니다"
**증상**: 메인 프로젝트에서 protocol_generated.h를 찾지 못함

**해결**:
1. **프로젝트 종속성 설정**:
   - Tiny_Game_Server (또는 메인 프로젝트) 우클릭
   - Build Dependencies → Project Dependencies
   - Protocol 체크박스 선택

2. **Include 경로 설정**:
   프로젝트 속성 → C/C++ → General → Additional Include Directories:
   ```
   $(SolutionDir)Protocol
   ```
   
   솔루션 구조에 따라:
   ```
   $(SolutionDir)..\Protocol    // 솔루션이 하위 폴더에 있는 경우
   ```

3. **빌드 순서 확인**:
   - Protocol 프로젝트를 먼저 빌드하여 protocol_generated.h 생성
   - 그 다음 메인 프로젝트 빌드

### 문제 6: "field names should be lowercase snake_case" 경고
**증상**: 빌드 시 네이밍 컨벤션 경고 발생

**해결**:
이미 `--no-warnings` 옵션이 적용되어 있습니다. 여전히 경고가 나온다면:
```xml
<Command>
  "$(ProjectDir)flatc.exe" --cpp --no-warnings -o "$(ProjectDir)" "%(FullPath)"
</Command>
```

### 문제 7: "protocol_generated.h이(가) 생성되지 않았습니다" 증분 빌드 경고
**증상**: 빌드는 성공했지만 증분 빌드가 제대로 작동하지 않음

**원인**: Outputs 경로가 실제 생성 경로와 다름

**해결**:
Protocol.vcxproj의 Outputs 경로 확인:
```xml
<Outputs Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
  $(ProjectDir)protocol_generated.h
</Outputs>
```

## 고급 사용법

### vcpkg 없이 수동 설치 (권장하지 않음)
vcpkg를 사용할 수 없는 환경이라면:

1. [FlatBuffers GitHub Releases](https://github.com/google/flatbuffers/releases)에서 다운로드
2. `include/flatbuffers` 폴더를 프로젝트에 복사
3. 프로젝트 속성에 Include 경로 수동 추가:
   ```
   E:\Game_Server\External\flatbuffers\include
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

#### Custom Build 명령 업데이트:
각 .fbs 파일에 대해 CustomBuild 항목 추가

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
      
      - name: Setup vcpkg
        run: |
          git clone https://github.com/Microsoft/vcpkg.git
          .\vcpkg\bootstrap-vcpkg.bat
          .\vcpkg\vcpkg install flatbuffers:x64-windows
      
      - name: Build Protocol
        run: |
          cd GServer_Project/Protocol
          flatc.exe --cpp --no-warnings protocol.fbs
      
      - name: Upload Generated Headers
        uses: actions/upload-artifact@v2
        with:
          name: protocol-headers
          path: GServer_Project/Protocol/protocol_generated.h
```

## 참고 자료

- [FlatBuffers 공식 문서](https://google.github.io/flatbuffers/)
- [vcpkg 공식 GitHub](https://github.com/Microsoft/vcpkg)
- [Visual Studio 커스텀 빌드 도구](https://docs.microsoft.com/en-us/cpp/build/reference/specifying-custom-build-tools)
- [MSBuild 커스텀 타겟](https://docs.microsoft.com/en-us/visualstudio/msbuild/msbuild-targets)

## 요약

✅ **vcpkg로 FlatBuffers 설치** (헤더 파일 자동 관리)  
✅ **Protocol 프로젝트 빌드** → protocol_generated.h 자동 생성  
✅ **의존성 설정**으로 항상 Protocol이 먼저 빌드  
✅ **--no-warnings** 옵션으로 경고 제거  
✅ **Protocol 폴더에 직접 생성**으로 구조 단순화  
✅ **수동 개입 없이 자동화된 워크플로우**

---

**프로토콜 변경 워크플로우:**
1. `protocol.fbs` 수정
2. Visual Studio에서 F7 (빌드)
3. 끝! (자동으로 헤더 생성)
