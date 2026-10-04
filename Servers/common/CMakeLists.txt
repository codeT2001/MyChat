# ==============================================================================
# common/CMakeLists.txt - 公共静态库 chat_common
# 包含：config_manager / redis_manager / mysql_dao/manager / asio_io_context_pool
#       status_grpc_client / verify_grpc_client / proto 生成代码
# ==============================================================================

# --- 依赖查找（与 server 端一致） ---
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC   CONFIG REQUIRED)
find_package(Boost  REQUIRED COMPONENTS filesystem)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
pkg_check_modules(HIREDIS_SSL REQUIRED IMPORTED_TARGET hiredis_ssl)
find_library(MYSQL_CPPCONN_LIB NAMES mysqlcppconn libmysqlcppconn)
find_library(JSONCPP_LIB NAMES jsoncpp libjsoncpp)

# --- protoc / grpc_cpp_plugin 可执行文件 ---
find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

# ==============================================================================
# 1. 从 message.proto 生成 gRPC 代码（生成到 build 目录，不入源码树）
# ==============================================================================
set(PROTO_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/proto/message.proto)
set(GRPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/grpc_gen)
file(MAKE_DIRECTORY ${GRPC_GEN_DIR})

set(GENERATED_PROTO_FILES
    ${GRPC_GEN_DIR}/message.pb.cc
    ${GRPC_GEN_DIR}/message.pb.h
    ${GRPC_GEN_DIR}/message.grpc.pb.cc
    ${GRPC_GEN_DIR}/message.grpc.pb.h
)

add_custom_command(
    OUTPUT ${GENERATED_PROTO_FILES}
    COMMAND ${PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
        --cpp_out=${GRPC_GEN_DIR}
        --grpc_out=${GRPC_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC code from message.proto"
    VERBATIM
)

# ==============================================================================
# 2. 定义 chat_common 静态库
# ==============================================================================
file(GLOB_RECURSE COMMON_SRCS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(chat_common STATIC
    ${COMMON_SRCS}
    ${GENERATED_PROTO_FILES}
)

# --- 头文件搜索路径（PUBLIC：消费方也能用） ---
target_include_directories(chat_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include    # #include "chat/xxx.h"
        ${GRPC_GEN_DIR}                        # #include "message.grpc.pb.h"
        ${HIREDIS_INCLUDE_DIRS}
        ${HIREDIS_SSL_INCLUDE_DIRS}
)

# --- 链接库（PUBLIC：静态库消费方需要传递性链接） ---
target_link_libraries(chat_common
    PUBLIC
        gRPC::grpc++
        gRPC::grpc
        protobuf::libprotobuf
        Boost::filesystem
        Threads::Threads
        ${CMAKE_DL_LIBS}
        PkgConfig::HIREDIS_SSL
        PkgConfig::HIREDIS
        OpenSSL::SSL
        OpenSSL::Crypto
        redis++
        ${MYSQL_CPPCONN_LIB}
        ${JSONCPP_LIB}
)

message(STATUS "chat_common static library configured (sources: ${COMMON_SRCS})")
