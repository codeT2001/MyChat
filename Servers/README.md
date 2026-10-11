# Chat 服务端集群

## 简介

即时通讯系统的服务端，包含三个 C++17 业务服务（GateServer / ChatServer / StatusServer）、一个公共静态库 `common` 和一个 Node.js 验证服务（VerifyServer）。基础设施（Redis / MySQL / gRPC 客户端 / proto / 单例 / 常量）统一沉淀到 `common/`（编译为 `libchat_common.a`），业务服务只保留各自独有的 TCP / HTTP / gRPC 实现。

> **本目录是服务端的构建入口。** 仓库根目录没有 `CMakeLists.txt`，所有构建命令都在 `Servers/` 下执行。Qt 客户端代码位于同级目录 [../Client](../Client/README.md)。

### 核心特性

- **用户认证**：注册、登录、密码重置（邮箱验证码）
- **负载均衡接入**：StatusServer 按在线人数把新连接分配给最空闲的 ChatServer
- **分布式聊天节点**：ChatServer 可水平扩展，节点间通过 gRPC 跨服推送
- **好友系统**：搜索用户、申请、同意/拒绝（`action=1/2`），拒绝后重新申请自动复位
- **持久化与缓存**：MySQL 存用户与好友关系；Redis 存验证码 / token / 用户资料 / 路由 / 在线计数
- **顶号**：重复登录不拒绝，自动踢掉旧会话

### 技术栈

| 组件 | 技术 |
|---|---|
| 客户端 | Qt 6, C++17 |
| 服务端 | C++17, gRPC, Boost.Asio, MySQL, Redis |
| 公共库 | C++17 静态库 `chat_common`（含 gRPC/Boost/hiredis/redis++/mysqlcppconn/jsoncpp） |
| 验证服务 | Node.js, gRPC, ioredis, nodemailer |

---

## 系统架构

```mermaid
graph TB
    Client["ChatClient（Qt）"]

    subgraph accessLayer ["接入层"]
        Gate["GateServer<br/>HTTP 网关 :9090"]
    end

    subgraph coordLayer ["协调层"]
        Status["StatusServer<br/>gRPC :50052<br/>分配节点 / 颁发 token"]
        Verify["VerifyServer（Node.js）<br/>gRPC :50051<br/>邮箱验证码"]
    end

    subgraph chatLayer ["聊天层 · 可水平扩展"]
        Chat1["ChatServer1<br/>TCP :50061<br/>gRPC :50081"]
        Chat2["ChatServer2<br/>TCP :50062<br/>gRPC :50082"]
    end

    subgraph dataLayer ["数据层"]
        Redis[("Redis :6379<br/>token / 验证码 /<br/>用户路由 / 在线计数")]
        MySQL[("MySQL :3306<br/>user / user_id /<br/>friend_apply / friend")]
        Mail["SMTP 邮件服务"]
    end

    Client -- "① HTTP 注册/登录/验证码" --> Gate
    Client -. "④ TCP 长连接（自定义包头+JSON）" .-> Chat1
    Client -. "④ TCP 长连接" .-> Chat2

    Gate -- "gRPC GetVerifyCode" --> Verify
    Gate -- "gRPC GetChatServer / Login" --> Status
    Gate -- 读写 --> Redis
    Gate -- 读写 --> MySQL

    Verify -- 存/取验证码 --> Redis
    Verify -- 发送验证码 --> Mail

    Status -- token / 读在线计数 --> Redis

    Chat1 -- 资料/token/路由/计数 --> Redis
    Chat2 -- 资料/token/路由/计数 --> Redis
    Chat1 -- 申请/好友/用户 --> MySQL
    Chat2 -- 申请/好友/用户 --> MySQL
    Chat1 <-->|"gRPC ChatService<br/>跨服好友通知/消息转发"| Chat2
```

**三个 C++ server 都依赖 `libchat_common.a`**，公共代码通过 `target_link_libraries(... chat_common)` 传递。

### 端口一览

| 服务 | 协议 | 端口 | 说明 |
|---|---|---|---|
| GateServer | HTTP | 9090 | 客户端注册/登录/验证码入口 |
| VerifyServer | gRPC | 50051 | `VerifyService.GetVerifyCode` |
| StatusServer | gRPC | 50052 | `StatusService.GetChatServer / Login` |
| ChatServer1 | TCP / gRPC | 50061 / 50081 | 聊天节点 1（客户端口 / 节点间口） |
| ChatServer2 | TCP / gRPC | 50062 / 50082 | 聊天节点 2 |

---

## 项目结构

```
chat/
├── CMakeLists.txt                 # 顶层 CMake（add_subdirectory: common → 3 servers）
│
├── common/                        # 公共静态库 libchat_common.a
│   ├── CMakeLists.txt             # 定义静态库 + proto 自动生成
│   ├── include/chat/              # 公共头文件（用 #include "chat/xxx.h" 引用）
│   │   ├── noncopyable.h          #   - DISALLOW_COPY_MOVE 宏
│   │   ├── constants.h            #   - Defer / ErrorCodes / MSG_IDS / 申请状态与 action 常量
│   │   ├── log.h                  #   - 统一日志宏 LOG_DEBUG/INFO/WARN/ERROR
│   │   ├── config_manager.h       #   - INI 配置解析
│   │   ├── redis_manager.h        #   - Redis 连接池
│   │   ├── mysql_dao.h            #   - MySQL DAO + 连接池
│   │   ├── mysql_manager.h        #   - MySQL 管理器
│   │   ├── rpc_stub_pool.h        #   - gRPC stub 连接池模板
│   │   ├── asio_io_context_pool.h #   - Asio I/O 线程池
│   │   ├── status_grpc_client.h   #   - StatusServer gRPC 客户端
│   │   └── verify_grpc_client.h   #   - VerifyServer gRPC 客户端
│   ├── src/                       # 公共实现
│   └── proto/message.proto        # 唯一 proto 源（生成至 build/common/grpc_gen/）
│
├── ChatServer/                    # TCP 聊天服务（业务层，可多实例）
│   ├── main.cpp / config.ini / config2.ini
│   └── src/
│       ├── cserver.h/cpp          #   - TCP server，session 生命周期
│       ├── csession.h/cpp         #   - 连接会话，包头/JSON 读写
│       ├── msg_node.h/cpp         #   - 消息节点
│       ├── user_manager.h/cpp     #   - uid → session 映射
│       ├── logic_system.h/cpp     #   - 登录/搜人/好友申请/认证
│       ├── chat_grpc_client.h/cpp #   - 向其他 ChatServer 发起 gRPC
│       └── chat_service_impl.h/cpp#   - 处理其他 ChatServer 的 gRPC 通知
│
├── GateServer/                    # HTTP 网关服务（业务层）
│   ├── main.cpp / config.ini
│   └── src/
│       ├── cserver.h/cpp          #   - HTTP server
│       ├── http_connection.h/cpp  #   - HTTP 连接与请求分发
│       └── logic_system.h/cpp     #   - 注册/登录/重置/验证码 Handler
│
├── StatusServer/                  # gRPC 状态/协调服务（业务层）
│   ├── main.cpp / config.ini
│   └── src/status_service_impl.h/cpp  # 节点分配、token 颁发、负载刷新
│
├── VerifyServer/                  # Node.js 邮件验证服务（gRPC :50051）
│
├── build/                         # 统一构建目录
├── logs/                          # 各服务运行日志
├── start.sh / status.sh / stop.sh # 服务管理脚本
└── 文档：README.md / AGENTS.md / REFACTORING.md / DEPLOY.md
```

---

## 快速开始

> 完整部署流程（依赖安装、数据库初始化、冒烟验证、systemd）见 [DEPLOY.md](DEPLOY.md)。

```bash
# 依赖（Ubuntu 22.04；gRPC/Protobuf、redis++ 需按官方文档源码安装）
sudo apt install -y build-essential cmake ninja-build pkg-config libssl-dev zlib1g-dev \
    mysql-server redis-server libmysqlcppconn-dev libhiredis-dev libjsoncpp-dev libboost-all-dev

cd Servers                        # ⚠️ 入口是 Servers/，不是仓库根目录
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)

./start.sh    # Redis → VerifyServer → GateServer → ChatServer1/2 → StatusServer
./status.sh   # 查看状态
./stop.sh     # 反序停止
```

环境要求：CMake **3.20+**、GCC 12+（C++17）、Node.js 14+、MySQL 5.7+、Redis 5.0+、Boost 1.74+。

---

## 接口与协议

### HTTP 接口（GateServer :9090）

| 路径 | 方法 | 说明 |
|---|---|---|
| `/getVerifyCode` | POST | 获取邮箱验证码（gRPC 调 VerifyServer） |
| `/registerUser` | POST | 注册新用户 |
| `/resetPassword` | POST | 重置密码 |
| `/userLogin` | POST | 登录，返回 ChatServer 的 host/port 与 token |

### gRPC 接口（common/proto/message.proto）

```protobuf
service VerifyService { rpc GetVerifyCode(GetVerifyReq) returns (GetVerifyRsp); }

service StatusService {
  rpc GetChatServer(GetChatServerReq) returns (GetChatServerRsp);  // 分配节点 + 颁发 token
  rpc Login(LoginReq) returns (LoginRsp);                          // 校验 token（TCP 登录时由 ChatServer 调用）
}

service ChatService {  // ChatServer 节点间
  rpc NotifyAddFriend(AddFriendReq) returns (AddFriendRsp);
  rpc ReplyFriend(ReplyFriendReq) returns (ReplyFriendRsp);         // 已定义，暂无调用
  rpc SendChatMsg(SendChatMsgReq) returns (SendChatMsgRsp);         // 已定义，暂无调用
  rpc NotifyFriendAccepted(FriendAcceptedReq) returns (FriendAcceptedRsp); // action: 1同意 2拒绝
  rpc NotifyTextChatMsg(TextChatMsgReq) returns (TextChatMsgRsp);
  rpc NotifyKickUser(KickUserReq) returns (KickUserRsp);            // 顶号踢下线
}
```

### 错误码（ErrorCodes）

服务端与客户端共享的整数契约，定义在 `common/include/chat/constants.h`：

| 值 | 枚举 | 含义 |
|---|---|---|
| 0 | SUCCESS | 成功 |
| 1 | ERROR_JSON | JSON 解析/字段/action 非法 |
| 2 | RPC_FAILED | gRPC 调用失败 |
| 3 | VERIFYCODE_NOT_FOUND_OR_EXPIRED | 验证码不存在或过期 |
| 4 | USER_EXIST | 用户/邮箱已存在 |
| 5 | EMAIL_NOT_MATCH | 重置密码时姓名邮箱不匹配 |
| 6 | UPDATE_PASSWORD_FAILED | 更新密码失败 |
| 7 | PASSWORD_NOT_MATCH | 密码错误 |
| 8 | UID_INVALID | 用户不存在/资料拉取失败 |
| 9 | TOKEN_INVALID | token 无效 |
| 10 | USER_ALREADY_LOGIN | 保留值，现不使用（已改为顶号模式） |

> ⚠️ VerifyServer（Node.js）用另一套独立错误码编号，与上表**并非一一对应**，详见 [VerifyServer/README.md](VerifyServer/README.md)。

### 一次完整登录

客户端 `POST /userLogin` → GateServer 校验密码 → StatusServer 选出节点并颁发 token → 客户端与该 ChatServer 建立 TCP 长连接 → ChatServer 经 gRPC 调 StatusServer 完成 token 二次校验。各流程的详细时序图见下方文档索引。

> TCP 消息号（`MSG_IDS`）与字段语义见 [common/README.md](common/README.md) 与 [ChatServer/README.md](ChatServer/README.md#消息协议)。

---

## 故障排除

| 现象 | 排查 |
|---|---|
| `cmake: no such file or directory: CMakeLists.txt` | 你不在 `Servers/` 目录。根目录没有 CMakeLists |
| CMake 报版本过低（要求 3.20） | `cmake --version` 确认 ≥ 3.20；Ubuntu 20.04 自带过低需升级 |
| 编译找不到 `chat/xxx.h` | 确认在 `Servers/` 下构建，重新 `cmake -S . -B build -G Ninja` |
| MySQL 连接失败 / 2014 Commands out of sync | 核对 `config.ini` 的 `[MySQL]`；连接池已修复（健康检查消费结果集、异常连接丢弃），确认用最新代码 |
| 注册报错 / uid 异常 | 检查是否漏建 `user_id` 发号器表 |
| Redis 连接失败 | `redis-cli ping`；核对 `config.ini` 的 `[Redis]` |
| gRPC 连不上 | `pgrep -f "node.*server.js"`；看 `logs/` 下对应日志 |
| 登录成功但客户端连不上聊天节点 | `StatusServer/config.ini` 里的 ChatServer Host 不可达（本地部署需改成 `127.0.0.1`） |
| 端口占用 | 9090 / 50051 / 50052 / 50061 / 50062 / 50081 / 50082 |
| 配置加载失败 | 可执行文件与 `config.ini` 必须同目录（`build/<server>/`） |

日志统一格式 `[时间戳(毫秒)] [级别] 内容`，位于 `logs/`。更多部署排查见 [DEPLOY.md](DEPLOY.md)。

---

## 安全提示

以下问题在源码中真实存在，**上线前必须处理**：

| 问题 | 位置 | 建议 |
|---|---|---|
| 数据库账号密码明文 | 各 `config.ini` 的 `[MySQL]` | 改用环境变量或密文注入，配置不入库 |
| 邮箱 SMTP 授权码明文 | `VerifyServer/config.json` | 立即轮换，改用环境变量注入 |
| 密码存明文 | GateServer + MySQL | 改用 bcrypt / argon2 加盐哈希 |
| 无 gRPC 鉴权 | 各服务间 gRPC | 内网部署 + mTLS，或加服务间 token |
| 无 TLS | GateServer HTTP | 前置 Nginx 终止 TLS |

---

## 文档索引

| 文档 | 说明 |
|---|---|
| [AGENTS.md](AGENTS.md) | 面向 AI Agent 的项目指引（构建/约定/约束） |
| [DEPLOY.md](DEPLOY.md) | 部署与运维：依赖、数据库、构建、冒烟、systemd |
| [REFACTORING.md](REFACTORING.md) | 三 Server 解耦的设计决策与权衡 |
| [common/README.md](common/README.md) | 公共静态库：组件、proto 契约、Redis 键约定、基础设施行为 |
| [GateServer/README.md](GateServer/README.md) | HTTP 网关：流程图、注册/验证码/重置/登录时序 |
| [ChatServer/README.md](ChatServer/README.md) | TCP 聊天节点：流程图、登录校验、好友/聊天时序与状态机、消息协议 |
| [StatusServer/README.md](StatusServer/README.md) | 协调服务：负载刷新流程图、token 校验时序 |
| [VerifyServer/README.md](VerifyServer/README.md) | Node.js 验证服务：GetVerifyCode 流程、SMTP 重试、错误码 |
