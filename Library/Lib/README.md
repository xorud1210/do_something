# Library/Lib — 저장소에서 제외된 바이너리

이 폴더의 `.lib` / `.dll` 은 git 에 올리지 않는다.
FBX SDK 라이브러리가 파일 하나에 126~250MB 라 GitHub 의 100MB 파일 제한을 넘기 때문이다.

헤더(`Library/Include/`)는 커밋되어 있으므로, `.lib` 만 아래 구조로 채우면 빌드된다.

```
Library/Lib/
├─ DirectXTex/
│    DirectXTex.lib
│    DirectXTex_debug.lib
└─ FBX/
   ├─ debug/
   │    libfbxsdk-md.lib   libfbxsdk-mt.lib   libfbxsdk.lib   libfbxsdk.dll
   │    libxml2-md.lib     libxml2-mt.lib
   │    zlib-md.lib        zlib-mt.lib
   └─ release/
        (debug 와 동일한 구성)
```

## 출처

| 라이브러리 | 받는 곳 |
|---|---|
| FBX SDK (2019.x, VS2017 x64) | Autodesk FBX SDK 설치 후 `lib/vs2017/x64/` 에서 복사 |
| DirectXTex | https://github.com/microsoft/DirectXTex 빌드 산출물 |

## 참고

- 엔진이 실제로 링크하는 것은 `-md` (MD 런타임) 버전이다.
  `EnginePch.h` 의 `#pragma comment(lib, ...)` 참고.
- 경로는 `Client.vcxproj` 의 `LibraryPath` 가 `$(SolutionDir)Library/Lib/` 를 잡아준다.
