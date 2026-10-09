cmake_minimum_required(VERSION 3.16)
project(MapForge C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# ---- raylib (скачивается автоматически) ----
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW ON)

# ---- Lua 5.4 (скачивается и собирается как статическая библиотека) ----
FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
  GIT_SHALLOW ON)

FetchContent_MakeAvailable(raylib lua)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_lib STATIC ${LUA_SOURCES})
target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR})
if(UNIX)
  target_compile_definitions(lua_lib PRIVATE LUA_USE_POSIX)
  target_link_libraries(lua_lib PUBLIC m)
endif()

# ---- Редактор карт ----
add_executable(mapforge_editor src/editor.cpp)
target_link_libraries(mapforge_editor PRIVATE raylib)

# ---- Движок с Lua ----
add_executable(mapforge_engine src/engine.cpp)
target_link_libraries(mapforge_engine PRIVATE raylib lua_lib)
