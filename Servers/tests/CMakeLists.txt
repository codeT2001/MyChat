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
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
find_package(gRPC CONFIG REQUIRED)
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
target_include_directories(test_server_select PRIVATE
    ${REPO_SERVER_DIR}/common/include
    ${REPO_SERVER_DIR}/StatusServer/src)

# StatusServer 登录 token 流程（纯函数 + 接口依赖）
chat_add_test(test_status_flow
    ${TEST_SRC_DIR}/test_status_flow.cpp
    ${REPO_SERVER_DIR}/StatusServer/src/status_flow.cpp)
target_include_directories(test_status_flow PRIVATE
    ${REPO_SERVER_DIR}/common/include
    ${REPO_SERVER_DIR}/StatusServer/src)

# TCP 帧头编解码（ChatServer，无外部依赖）
chat_add_test(test_msg_codec
    ${TEST_SRC_DIR}/test_msg_codec.cpp
    ${REPO_SERVER_DIR}/ChatServer/src/msg_codec.cpp)
target_include_directories(test_msg_codec PRIVATE ${REPO_SERVER_DIR}/ChatServer/src)

# URL 解码 / 查询串解析（GateServer 纯逻辑）
chat_add_test(test_http_params
    ${TEST_SRC_DIR}/test_http_params.cpp
    ${REPO_SERVER_DIR}/GateServer/src/http_params.cpp)
target_include_directories(test_http_params PRIVATE ${REPO_SERVER_DIR}/GateServer/src)

# uid → 会话 注册表（模板，纯逻辑；覆盖顶号语义）
chat_add_test(test_session_registry
    ${TEST_SRC_DIR}/test_session_registry.cpp)
target_include_directories(test_session_registry PRIVATE ${REPO_SERVER_DIR}/common/include)

# 节点注册信息编解码（纯逻辑，不访问 Redis）
chat_add_test(test_node_info
    ${TEST_SRC_DIR}/test_node_info.cpp
    ${REPO_SERVER_DIR}/common/src/node_info.cpp)
target_include_directories(test_node_info PRIVATE ${REPO_SERVER_DIR}/common/include)
target_link_libraries(test_node_info PRIVATE ${JSONCPP_LIB})

# 跨节点 RPC 派发策略（同一目标固定同一 worker）
chat_add_test(test_rpc_dispatch
    ${TEST_SRC_DIR}/test_rpc_dispatch.cpp)
target_include_directories(test_rpc_dispatch PRIVATE
    ${REPO_SERVER_DIR}/common/include
    ${REPO_SERVER_DIR}/ChatServer/src)

# 业务线程分片：同一 session 固定落到同一 worker
chat_add_test(test_business_worker
    ${TEST_SRC_DIR}/test_business_worker.cpp)
target_include_directories(test_business_worker PRIVATE ${REPO_SERVER_DIR}/ChatServer/src)

# 跨节点事件类型名称映射
chat_add_test(test_chat_event
    ${TEST_SRC_DIR}/test_chat_event.cpp
    ${REPO_SERVER_DIR}/ChatServer/src/chat_event.cpp)
target_include_directories(test_chat_event PRIVATE ${REPO_SERVER_DIR}/ChatServer/src)

# gRPC stub 池（RAII 归还 / 池耗尽超时 / 池先析构）
chat_add_test(test_rpc_stub_pool
    ${TEST_SRC_DIR}/test_rpc_stub_pool.cpp)
target_include_directories(test_rpc_stub_pool PRIVATE ${REPO_SERVER_DIR}/common/include)
target_link_libraries(test_rpc_stub_pool PRIVATE gRPC::grpc++ Threads::Threads)
