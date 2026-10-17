# ==============================================================================
# tests/CMakeLists.txt - 单元测试（CTest 驱动，零外部框架依赖）
#
# 只编译被测的「纯逻辑」源文件，不依赖 DB/Redis/gRPC，保证测试快速、可离线运行。
# 运行方式：
#   cmake -S . -B build && cmake --build build -j1
#   ctest --test-dir build --output-on-failure
# ==============================================================================

set(TEST_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(REPO_SERVER_DIR ${CMAKE_SOURCE_DIR})

# 导入目标（OpenSSL::Crypto / Boost::filesystem）只在其 find_package 所在目录作用域可见，
# 这里显式再找一次；结果已缓存，开销可忽略。
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(Threads REQUIRED)
find_library(JSONCPP_LIB NAMES jsoncpp libjsoncpp)

# 组装一个测试可执行文件（test_main.cpp 提供 main）并注册到 CTest
function(chat_add_test name)
    add_executable(${name} ${TEST_SRC_DIR}/test_main.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_SRC_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# 口令摘要 / 恒定时间比较（依赖 OpenSSL）
chat_add_test(test_password_crypto
    ${TEST_SRC_DIR}/test_password_crypto.cpp
    ${REPO_SERVER_DIR}/common/src/password_crypto.cpp)
target_include_directories(test_password_crypto PRIVATE ${REPO_SERVER_DIR}/common/include)
target_link_libraries(test_password_crypto PRIVATE OpenSSL::Crypto)

# MsgNode 裸缓冲（ChatServer，无外部依赖）
chat_add_test(test_msg_node
    ${TEST_SRC_DIR}/test_msg_node.cpp
    ${REPO_SERVER_DIR}/ChatServer/src/msg_node.cpp)
target_include_directories(test_msg_node PRIVATE ${REPO_SERVER_DIR}/ChatServer/src)

# 协议常量契约（纯头文件）
chat_add_test(test_protocol_constants
    ${TEST_SRC_DIR}/test_protocol_constants.cpp)
target_include_directories(test_protocol_constants PRIVATE ${REPO_SERVER_DIR}/common/include)

# 配置解析（依赖 Boost）
chat_add_test(test_config_manager
    ${TEST_SRC_DIR}/test_config_manager.cpp
    ${REPO_SERVER_DIR}/common/src/config_manager.cpp)
target_include_directories(test_config_manager PRIVATE ${REPO_SERVER_DIR}/common/include)
target_link_libraries(test_config_manager PRIVATE Boost::filesystem Threads::Threads)

# GateServer 业务逻辑（纯函数 + 接口依赖；测试注入 fake，不碰 DB/Redis/gRPC）
chat_add_test(test_gate_flow
    ${TEST_SRC_DIR}/test_gate_flow.cpp
    ${REPO_SERVER_DIR}/GateServer/src/gate_flow.cpp)
target_include_directories(test_gate_flow PRIVATE
    ${REPO_SERVER_DIR}/common/include
    ${REPO_SERVER_DIR}/GateServer/src)
target_link_libraries(test_gate_flow PRIVATE ${JSONCPP_LIB})

# ChatServer 业务逻辑（纯函数 + 接口依赖；测试注入 fake，不碰 DB/Redis）
chat_add_test(test_chat_flow
    ${TEST_SRC_DIR}/test_chat_flow.cpp
    ${REPO_SERVER_DIR}/ChatServer/src/chat_flow.cpp)
target_include_directories(test_chat_flow PRIVATE
    ${REPO_SERVER_DIR}/common/include
    ${REPO_SERVER_DIR}/ChatServer/src)
target_link_libraries(test_chat_flow PRIVATE ${JSONCPP_LIB})

# StatusServer 选点逻辑（纯函数，仅依赖头文件）
chat_add_test(test_server_select
    ${TEST_SRC_DIR}/test_server_select.cpp
    ${REPO_SERVER_DIR}/StatusServer/src/server_select.cpp)
target_include_directories(test_server_select PRIVATE ${REPO_SERVER_DIR}/StatusServer/src)
