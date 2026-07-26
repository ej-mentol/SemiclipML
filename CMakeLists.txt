cmake_minimum_required(VERSION 3.10)
project(SemiclipML)

set(CMAKE_CXX_STANDARD 17)

# --- Third-party: single submodule (Bots-United/metamod-p, bundles patched HLSDK) ---
# Layout auto-detection: current master keeps hlsdk/ and metamod/ at the repo
# root, but tolerate an include/ prefix in case the layout ever changes.
set(MMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod-p")

if(EXISTS "${MMP_ROOT}/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/hlsdk")
elseif(EXISTS "${MMP_ROOT}/include/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/include/hlsdk")
else()
    message(FATAL_ERROR
        "HLSDK not found under thirdparty/metamod-p (checked hlsdk/ and include/hlsdk/).\n"
        "If the submodule is empty, run: git submodule update --init --recursive\n"
        "(or clone with: git clone --recursive <repo-url>)")
endif()

if(EXISTS "${MMP_ROOT}/metamod/meta_api.h")
    set(MMP_META "${MMP_ROOT}/metamod")
elseif(EXISTS "${MMP_ROOT}/include/metamod/meta_api.h")
    set(MMP_META "${MMP_ROOT}/include/metamod")
else()
    message(FATAL_ERROR
        "metamod headers not found under thirdparty/metamod-p (checked metamod/ and include/metamod/).\n"
        "If the submodule is empty, run: git submodule update --init --recursive")
endif()

message(STATUS "Using HLSDK:   ${MMP_SDK}")
message(STATUS "Using metamod: ${MMP_META}")

# SYSTEM: suppress warnings coming from third-party headers (metamod-p's
# DLLINTERNAL visibility attribute trips -Wattributes on modern gcc) while
# keeping full warnings for our own sources.
include_directories(SYSTEM ${MMP_SDK}/common)
include_directories(SYSTEM ${MMP_SDK}/dlls)
include_directories(SYSTEM ${MMP_SDK}/engine)
include_directories(SYSTEM ${MMP_SDK}/pm_shared)
include_directories(SYSTEM ${MMP_META})

# Determine OS
if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
else()
    add_definitions(-Dlinux)
    # GoldSrc is 32-bit; -m32 must apply to compile AND link.
    # -mincoming-stack-boundary=2: the engine/metamod call in with 4-byte
    # stack alignment, modern gcc assumes 16 - without this you get random
    # runtime crashes, not build errors (same fix metamod-p applies to its
    # own example plugins).
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -march=i686 -mincoming-stack-boundary=2 -fno-strict-aliasing")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
endif()

# Project Sources (Only our code)
set(PROJECT_SRCS
    src/Main.cpp
    src/Config.cpp
    src/Semiclip.cpp
)

add_library(SemiclipML SHARED ${PROJECT_SRCS})

# No extra libs needed for pure metamod plugin usually, maybe standard system ones implicitly
if(WIN32)
    target_link_libraries(SemiclipML user32)
else()
    # SemiclipML.so instead of libSemiclipML.so - matches plugins.ini habits.
    set_target_properties(SemiclipML PROPERTIES PREFIX "")
endif()
