# ==============================================================================
# GateServer/CMakeLists.txt - HTTP 网关服务（业务层）
# 公共依赖通过 link chat_common 传递
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(gateServer
    VERSION 0.1.0
    DESCRIPTION "Gate Server (HTTP)"
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

# --- 将 config.ini 复制到可执行文件同目录（build/GateServer/） ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.ini
    ${CMAKE_CURRENT_BINARY_DIR}/config.ini
    COPYONLY
)

# --- 业务头文件搜索路径 ---
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- 链接公共库 ---
target_link_libraries(${PROJECT_NAME} PRIVATE chat_common)

# --- Debug 选项 ---
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_MODE=1)
endif()

message(STATUS "Configured ${PROJECT_NAME}")
