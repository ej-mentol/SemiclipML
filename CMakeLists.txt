cmake_minimum_required(VERSION 3.10)
project(SemiclipML)

set(CMAKE_CXX_STANDARD 17)

include_directories(thirdparty/sdk/common)
include_directories(thirdparty/sdk/dlls)
include_directories(thirdparty/sdk/engine)
include_directories(thirdparty/sdk/pm_shared)
include_directories(thirdparty/metamod)

# Determine OS
if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
else()
    add_definitions(-Dlinux)
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
endif()
