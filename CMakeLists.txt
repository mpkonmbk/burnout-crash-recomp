# burnoutcrash - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(burnoutcrash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The codegen configs (config/*.toml) and hooks match one exact build of the game: the XBLA
# release with title update 3. Refuse anything else instead of producing a binary that crashes.
set(BURNOUTCRASH_GAME_FILES
    "DEFAULT.XEX|f2389f9b8655e6432a78db8a4adc4cf524e5c56a6a92d013b6f0fb0452d4b425"
    "DLL/CRASH.DLL.XEX|bf66a2d38cf9a79dfe20558d289edb96b9f2346bf344613946a4ada897298338")
foreach(entry IN LISTS BURNOUTCRASH_GAME_FILES)
    string(REPLACE "|" ";" entry "${entry}")
    list(GET entry 0 game_file)
    list(GET entry 1 expected_sha256)
    set(game_path "${CMAKE_CURRENT_SOURCE_DIR}/game/${game_file}")
    if(NOT EXISTS "${game_path}")
        message(FATAL_ERROR "Missing game/${game_file}. Extract your own copy of Burnout CRASH! "
                            "into game/ first (see README.md).")
    endif()
    file(SHA256 "${game_path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR "game/${game_file} is a different build than this project supports "
                            "(SHA-256 ${actual_sha256}, expected ${expected_sha256}). "
                            "See README.md for the supported version.")
    endif()
endforeach()

include(generated/rexglue.cmake)

# Mid-asm hooks referenced by the recompiled DLL (see config/crash_dll.toml)
if(TARGET burnoutcrash_CRASH_DLL)
    target_sources(burnoutcrash_CRASH_DLL PRIVATE src/switch_hooks.cpp)
endif()

# Sources
set(BURNOUTCRASH_SOURCES
    src/main.cpp
    src/crash_trace.cpp
    src/guest_overrides.cpp
)

if(WIN32)
    add_executable(burnoutcrash WIN32 ${BURNOUTCRASH_SOURCES})
else()
    add_executable(burnoutcrash ${BURNOUTCRASH_SOURCES})
endif()

target_compile_definitions(burnoutcrash PRIVATE
    BURNOUTCRASH_DEFAULT_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game")

rexglue_setup_target(burnoutcrash GPU_PLUGINS xenos)

if(WIN32)
    target_link_libraries(burnoutcrash PRIVATE dbghelp)  # src/crash_trace.cpp
endif()
