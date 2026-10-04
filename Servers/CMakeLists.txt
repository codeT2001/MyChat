# ==============================================================================
# 顶层 CMakeLists.txt - chat 项目（公共库 + 三个 server）
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(chat
    VERSION 0.1.0
    DESCRIPTION "Chat system: GateServer + ChatServer + StatusServer + common library"
    LANGUAGES CXX
)

# --- 全局配置 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 注意：不统一设置 CMAKE_RUNTIME_OUTPUT_DIRECTORY
# 让每个 server 的可执行文件落在自己的 build/<server>/ 子目录，
# 这样各自的 config.ini（也复制到该子目录）不会互相覆盖。

# --- 子项目 ---
# 注意顺序：common 必须先于 server，server 通过 target_link_libraries(... chat_common)
# 拿到 chat_common 的 PUBLIC 依赖（gRPC/Boost/hiredis/redis++/mysql/jsoncpp）
add_subdirectory(common)
add_subdirectory(ChatServer)
add_subdirectory(GateServer)
add_subdirectory(StatusServer)

message(STATUS "=== chat project configuration complete ===")
