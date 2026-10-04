# ==============================================================================
# ChatServer/CMakeLists.txt - TCP 聊天服务（业务层）
# 公共依赖（gRPC/Redis/MySQL/Boost/Asio 等）通过 link chat_common 传递
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(chatServer
    VERSION 0.1.0
    DESCRIPTION "Chat Server (TCP)"
    LANGUAGES CXX
)

# --- 收集业务源文件（src/ 下） ---
file(GLOB_RECURSE SRC_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${SRC_FILES}
)

# --- 将 config.ini 复制到可执行文件同目录（build/ChatServer/） ---
set(CHAT_SERVER_CONFIGS
    config.ini
    config2.ini
)

foreach(CONFIG_FILE ${CHAT_SERVER_CONFIGS})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_FILE}
        COPYONLY
    )
endforeach()

# --- 业务头文件搜索路径 ---
# 公共库的 include（chat/*.h / message.grpc.pb.h / hiredis 等）通过 chat_common PUBLIC 自动传递
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}       # 根目录（main.cpp 引用 "cserver.h"）
        ${CMAKE_CURRENT_SOURCE_DIR}/src   # 业务源码目录
)

# --- 链接公共库 ---
target_link_libraries(${PROJECT_NAME} PRIVATE chat_common)

# --- Debug 选项 ---
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_MODE=1)
endif()

message(STATUS "Configured ${PROJECT_NAME}")
