cmake_minimum_required(VERSION 3.15)
project(SemiclipML)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Third-party: single submodule (Bots-United/metamod-p, bundles patched HLSDK) ---
set(MMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod-p")

if(EXISTS "${MMP_ROOT}/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/hlsdk")
elseif(EXISTS "${MMP_ROOT}/include/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/include/hlsdk")
else()
    message(FATAL_ERROR
        "HLSDK not found under thirdparty/metamod-p (checked hlsdk/ and include/hlsdk/).\n"
        "If the submodule is empty, run: git submodule update --init --recursive")
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

include_directories(SYSTEM ${MMP_SDK}/common)
include_directories(SYSTEM ${MMP_SDK}/dlls)
include_directories(SYSTEM ${MMP_SDK}/engine)
include_directories(SYSTEM ${MMP_SDK}/pm_shared)
include_directories(SYSTEM ${MMP_META})

# Compiler warnings and runtime settings
if(MSVC)
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /GR- /EHs-c- /external:anglebrackets /external:W0)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_definitions(-Dlinux)
    add_compile_options(-Wall -Wextra -Wshadow -fno-exceptions -fno-rtti)
    # GoldSrc is 32-bit; -m32 must apply to compile AND link.
    # -mincoming-stack-boundary=2: engine/metamod use 4-byte stack alignment.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -march=i686 -mincoming-stack-boundary=2 -fno-strict-aliasing -fvisibility=hidden -fvisibility-inlines-hidden")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32 -static-libgcc -static-libstdc++ -Wl,--exclude-libs,ALL -Wl,--no-undefined")
endif()

# Project Sources
set(PROJECT_SRCS
    src/Main.cpp
    src/Config.cpp
    src/Config.h
    src/Semiclip.cpp
    src/Semiclip.h
)

add_library(SemiclipML SHARED ${PROJECT_SRCS})

if(NOT WIN32)
    set_target_properties(SemiclipML PROPERTIES PREFIX "")
endif()
