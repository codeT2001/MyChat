# Chat 分布式即时通讯系统 · 架构与逻辑详述

> 本文是完整的系统架构与运行逻辑说明，覆盖服务端集群与 Qt6 客户端。
> 基于对全部源码（138 文件 / 约 11,340 行）的通读整理，关键链路已逐行核实。
> 配套文档：`chat项目改进建议汇总.md`（问题与改进唯一清单）、`chat网络传输与中间件改进.md`（网络专项）。

---

## 目录

- [一、系统总览](#一系统总览)
- [二、分层架构](#二分层层架构)
- [三、服务端各组件详解](#三服务端各组件详解)
- [四、客户端详解](#四客户端详解)
- [五、通信协议](#五通信协议)
- [六、核心业务链路](#六核心业务链路)
- [七、数据模型](#七数据模型)
- [八、并发与线程模型](#八并发与线程模型)
- [九、启动与运维](#九启动与运维)

---

## 一、系统总览

### 1.1 是什么

一个面向教学/实战的**分布式 IM 系统**，最大特点是 **"客户端轻、服务端重"** —— 把负载均衡、集群扩展、跨服路由等分布式难题放在服务端，客户端只做两件事：**HTTP 接入** + **TCP 长连接**。

### 1.2 规模

| 指标 | 数值 |
|------|------|
| 源码总行数 | 11,340 |
| 源文件数 | 138 |
| 服务组件 | 5（Gate / Chat / Status / Verify + 公共库） |
| HTTP 接口 | 4 |
| TCP 业务消息号 | 16 + 1（心跳） |
| gRPC 方法 | 6（ChatService） + 2（StatusService） + 1（VerifyService） |

### 1.3 仓库结构

```
chat/
├── Client/                     # Qt6 / C++17 桌面客户端（约 6.4k 行，86 文件）
│   ├── src/ inc/               #   实现与头文件
│   ├── ui/ style/ images/      #   Designer 界面(18) / QSS(17) / 图片(60+)
│   └── README.md
│
└── Servers/                    # 服务端集群（约 4.9k 行）
    ├── common/                 #   公共静态库 libchat_common.a
    ├── GateServer/             #   HTTP 网关（Boost.Beast :9090）
    ├── ChatServer/             #   TCP 聊天节点（可多实例）
    ├── StatusServer/           #   gRPC 协调服务（:50052）
    ├── VerifyServer/           #   Node.js 邮件验证码（:50051）
    └── start.sh / stop.sh / status.sh / logs/
```

### 1.4 技术栈

| 部分 | 技术 |
|------|------|
| 客户端 | Qt6 Widgets、C++17、CMake（AUTOMOC/UIC/RCC）、QNetworkAccessManager、QTcpSocket |
| 服务端 | C++17、Boost.Asio / Beast / UUID、gRPC / Protobuf |
| 数据层 | MySQL（mysqlcppconn）、Redis（sw::redis++ / hiredis）、jsoncpp |
| 验证服务 | Node.js、@grpc/grpc-js、ioredis、nodemailer |

---

## 二、分层架构

系统自上而下分为四层：**接入层、协调层、聊天层、数据层**。

```
┌─────────────────────────────────────────────────────────┐
│                    客户端（Qt6）                          │
│   QNetworkAccessManager（HTTP）  +  QTcpSocket（TCP）     │
└───────┬─────────────────────────────────┬───────────────┘
        │ ① HTTP：注册/登录/验证码/改密     │ ③ TCP 长连接：好友/聊天
        ▼                                 ▼
┌───────────────────┐            ┌────────────────────────┐
│  【接入层】        │            │  【聊天层】可水平扩展   │
│  GateServer       │            │  ChatServer1  ChatServer2│
│  HTTP :9090       │            │  TCP:50061/2  gRPC:50081/2│
└───┬───────────┬───┘            └───────────┬────────────┘
    │ gRPC      │ 读写                        │ 读写 + 节点间 gRPC
    ▼           ▼                             ▼
┌───────────────────────┐            ┌────────────────────────┐
│  【协调层】            │            │  【数据层】             │
│  StatusServer :50052  │◀──────────▶│  Redis :6379            │
│  VerifyServer :50051  │            │  MySQL :3306            │
└───────────────────────┘            │  SMTP 邮件服务          │
                                     └────────────────────────┘
```

**设计精髓**：登录只走一次 HTTP（拿 token + 节点地址），之后所有好友/聊天业务走 TCP 长连接，避免每次操作都做 HTTP 握手。

---

## 三、服务端各组件详解

### 3.1 公共库 libchat_common.a

三个 C++ 服务共享的基础设施，是"公共静态库 + 瘦服务"组织的核心（`CMakeLists.txt` 先 `add_subdirectory(common)`）。

| 组件 | 文件 | 职责与设计 |
|------|------|-----------|
| `mysql_dao` | `common/src/mysql_dao.cpp` | DAO + 连接池。固定大小阻塞队列、`wait_for(3s)` 超时、后台健康检查、全 PreparedStatement |
| `redis_manager` | `common/src/redis_manager.cpp` | Redis 操作封装（string/hash/list），底层自带连接池 |
| `rpc_stub_pool` | `common/include/chat/rpc_stub_pool.h` | gRPC stub 连接池模板，共享 channel + 多 stub |
| `asio_io_context_pool` | `common/include/chat/asio_io_context_pool.h` | 线程池，固定 2 线程，round-robin 分配 |
| `config_manager` | `common/src/config_manager.cpp` | Meyers 单例 + property_tree 解析 INI |
| `log` | `common/include/chat/log.h` | 日志宏，输出 stderr，mutex 保护 |

> 仓库内 `REFACTORING.md` 完整记录了从"三份拷贝"到"公共静态库"的解耦决策。

### 3.2 GateServer（HTTP 网关）

**技术**：Boost.Beast（HTTP）｜**端口**：9090

**启动与连接模型**（`main.cpp`）：
- 单线程 `io_context io{1}` 跑主循环，只负责 `async_accept`。
- 每个新连接被绑到 `AsioIOContextPool` 的某个 `io_context` 上 → **accept 单线程 + 业务多线程**。

**请求处理流程**（`http_connection.cpp`）：
```
Start() → async_read 读完整请求
  → HandleRequest()
      ├─ GET : PreParseGetParams() 解析 query → LogicSystem::HandleGet
      ├─ POST: LogicSystem::HandlePost → 对应 handler
      └─ WriteResponse() → async_write → shutdown → 取消 60s 定时器
  → CheckDeadline()  60s 超时关闭 socket
```
> 请求-响应为**短连接**：`response_.keep_alive(false)`。

**4 个 POST handler**（`logic_system.cpp:19-23` 注册）：

| 路径 | 函数 | 逻辑 |
|------|------|------|
| `/getVerifyCode` | `HandleGetVerifyCode` | 解析 email → gRPC 调 VerifyServer |
| `/registerUser` | `HandleRegisterUser` | 比对 Redis 验证码 → MySQL 建用户 |
| `/resetPassword` | `HandleResetPassword` | 验证码 + name/email 校验 → 改密 |
| `/userLogin` | `HandleUserLogin` | 校验密码 → 防重登 → 分配节点 + 发 token |

**代码亮点**：`Defer`（RAII 辅助类）在函数任意 return 分支统一写回响应体，避免漏写。

### 3.3 ChatServer（TCP 聊天节点）

**技术**：Boost.Asio（TCP）+ gRPC（节点间）｜**端口**：TCP 50061/2 · gRPC 50081/2

**核心类**：

| 类 | 文件 | 职责 |
|----|------|------|
| `CServer` | `cserver.cpp` | acceptor + session 表（`unordered_map<uint32_t, weak_ptr<CSession>>`） |
| `CSession` | `csession.cpp` | 单连接读写、4 字节帧编解码、发送队列 |
| `LogicSystem` | `logic_system.cpp` | 业务单线程队列、msgId 分发 |
| `UserManager` | `user_manager.cpp` | `uid → session` 映射 |
| `ChatServiceImpl` | `chat_service_impl.cpp` | gRPC 服务端（被跨服调用时推送本节点用户） |
| `ChatGrpcClient` | `chat_grpc_client.cpp` | 跨服 gRPC 客户端（每对端一个 stub 池） |

**消息分发（生产-消费模型）**：
```
I/O 线程收到完整帧
  → LogicSystem::PostMsgToQue(LogicNode)      # 入队
      → DealMessage() 单线程消费              # 出队
          → ProcessMessage() 按 msgId 分发     # unordered_map<MSG_IDS, handleFunc>
              ├─ HandleLogin
              ├─ HandleSearchUser
              ├─ HandleAddFriend
              ├─ HandleAuthFriend
              └─ HandleTextChatMsg
```

**5 个业务 handler 逻辑**：
- `HandleLogin`：token 校验 → `GetBaseUserInfo`（Redis 优先，miss 回源 MySQL）→ 返回资料 + 申请列表 + 好友列表 → 绑定 session + 写路由键 + 计数 +1。
- `HandleAddFriend`：落库申请 → 查对端 `userver_` 路由 → 同服直推 / 跨服 gRPC。
- `HandleAuthFriend`：`action` 1/2 → 更新申请状态 → 同意才 `AddFriend` → 同服推送 / 跨服通知。
- `HandleTextChatMsg`：查对端路由 → 同服直推 `NOTIFY_TEXT_CHAT_MSG_REQ` / 跨服 `NotifyTextChatMsg`。

**优雅停机**（`main.cpp`）：信号回调只做 `main_io.stop()`，真正的清理在 `run()` 返回后按序执行——`HDel(LOGIN_COUNT)` → `pool.Stop()` → `grpc_server->Shutdown()` → join。

**会话清理竞态处理**：`RmvUserSession(uid, session)` 仅当 map 中绑定的正是该 session 才移除——防止"被顶掉的旧会话"断开时误删新会话的 Redis 状态与路由键。

### 3.4 StatusServer（协调服务）

**技术**：gRPC｜**端口**：50052

**两个 RPC**：
- `GetChatServer(uid)`：返回最空闲节点 + 生成 UUID token → 写 Redis `utoken_{uid}`。
- `Login(uid, token)`：从 Redis 读 token 比对（当前业务链路未调用）。

**最小连接数负载均衡（核心）** —— `RefreshCache`（每秒后台刷新一次）：
```
Step 1  加共享锁，拷贝 servers_ 快照（读锁不阻塞请求）
Step 2  HGetAll(login_count) 一次性拉取所有节点在线计数（IO 在后台线程）
Step 3  遍历快照，con_count = stoi(count_map[name])，挑最小值 → minServer
Step 4  加独占锁，servers_ = move(snapshot); minServer_ = minServer（写锁极短）
```
请求侧 `GetChatServer()` 只读 `minServer_`（共享锁），**几乎零开销**。

> 设计精髓：**后台算好、请求直接读缓存值** —— 避免每个登录请求都做 `HGetAll`，锁竞争降到最低。`ChatServer` 类重载 `operator<` 按 `con_count` 比较。

### 3.5 VerifyServer（验证码服务）

**技术**：Node.js（grpc-js）｜**端口**：50051

**验证码流程**（`server.js`）：
```
GetVerifyCode(email):
  Redis.Get("code_"+email)
    ├─ 已存在 → 返回「验证码已存在」，不重发        # 防刷
    └─ 不存在 → uuidv4()[0:4] 取前4位做验证码
                 SetRedisExpire(key, code, 600)     # 10 分钟
                 SendMail({to, subject, text})
                 return SUCCESS
```
- SMTP：单连接 nodemailer，对 `ETIMEDOUT` 做最多 5 次指数退避重试并重建 transport。
- 优雅退出：`SIGINT/SIGTERM → GracefulShutdown → email.close() → exit(0)`。

---

## 四、客户端详解

### 4.1 分层结构

Qt6 工程，五层解耦（`main.cpp` 注释即设计意图："解耦网络处理层、UI层、JSON处理层、工具层"）：

| 层 | 核心类 | 职责 |
|----|--------|------|
| **网络层**（单例） | `HttpManager` / `TcpManager` | HTTP 请求封装 / TCP 帧编解码+心跳+重连 |
| **业务服务层** | `AuthService` / `FriendService` / `ChatService` | 登录编排 / 好友业务 / 消息收发 |
| **编解码层** | `JsonParser` / `JsonSerializer` | 纯函数式 JSON 转换，不依赖网络与 UI |
| **数据层**（单例） | `UserManager` | 当前用户、token、申请/好友表、分页游标 |
| **UI 层** | `MainWindow` 及各 Dialog/Page/自定义控件 | 窗口容器、页面切换、自绘气泡/列表 |

### 4.2 网络层

**HTTP（`httpmanager.cpp`）**：封装 `QNetworkAccessManager`，统一 POST JSON，结果经 `SigHttpFinish` 按 `Modules{REGISTER/RESET/LOGIN}` 转发。

**TCP（`tcpmanager.cpp`）**：
- **纯传输层**：只拆帧不解析业务，把 `(msgId, body)` 通过 `SigMessageReceived` 广播给各 Service，由 Service 认领自己的 msgId（多播分发）。
- **收包状态机**：`recvPedding_` 标志实现跨 `readyRead` 的**半包续读**；1MB 接收缓冲上限 + 单包 10MB 上限两道防线。
- **帧编解码**：`QDataStream` 设 BigEndian，`out << msgId << len`，与后端对应。
- **心跳**：连接成功即启动，每 30s 发空 body 的 `CHAT_HEARTBEAT(99999)`。
- **断线重连**：指数退避 1s→2s→…→60s，最多 10 次。

### 4.3 UI 结构

```
MainWindow（"壳"，动态切换四页）
 ├─ LoginDialog      登录
 ├─ RegisterDialog   注册
 ├─ ResetDialog      改密
 └─ ChatWindow       登录成功后
       ├─ 左侧列表区：chatsPage / searchPage / contactsPage
       └─ 右侧内容区：chatDataEmptyPage / chatPage / infoPage / applyFriendPage
```
- 页面切换为"销毁旧页 + 懒创建新页"模式。
- 侧边栏两个 `BadgeButton`（带红点自绘）组成 `QButtonGroup` 互斥切换会话/通讯录模式。
- **自绘控件体系**：`ChatItemBase → BubbleFrame → TextBubble/PictureBubble` 气泡三级嵌套。

### 4.4 数据管理

**全部内存态，无任何持久化**（无数据库/文件/Settings）：
- `UserManager`（单例）：`token_`、`userInfo_`、`applyList_`（uid 去重）、`friendMap_`。
- `FriendInfo` 即"会话聚合根"：内嵌 `desc_`（签名）、`label_`、`last_msg_`、`chat_msgs_`（全部历史消息）。
- **分页游标**：会话列表/通讯录各维护 `chatLoaded_`/`contactLoaded_`，每页 13 条。
- 备注/标签仅本地，服务端同步协议待接入。

---

## 五、通信协议

### 5.1 HTTP（客户端 → GateServer）

4 个 POST 接口，均 JSON body + JSON 响应，路径见 [3.2](#32-gateserverhttp-网关)。

### 5.2 TCP 帧（客户端 ↔ ChatServer）

```
┌─────────────┬─────────────┬──────────────────────┐
│  msgId (2B) │  len (2B)   │   JSON body (len 字节) │
│  大端 uint16 │  大端 uint16 │     UTF-8 JSON        │
└─────────────┴─────────────┴──────────────────────┘
头部共 4 字节（HEAD_TOTAL_LENGTH）
```
- 发送：`htons` 写大端；接收：两阶段异步读（先读 4B 包头，再按 bodySize 读 body）。
- 单帧 body 上限：服务端 `MAX_MSG_LENGTH = 2048`；`uint16` 硬上限 65535。

**消息号表**：

| 消息号 | 名称 | 方向 |
|--------|------|------|
| 10001/10002 | CHAT_LOGIN_REQ / RSP | 登录聊天服务器 |
| 10003/10004 | SEARCH_USER_REQ / RSP | 搜索用户 |
| 10005/10006 | ADD_FRIEND_REQ / RSP | 发起好友申请 |
| 10007/10008 | NOTIFY_ADD_FRIEND_REQ / RSP | 被申请方收到通知 |
| 10009/10010 | AUTH_FRIEND_REQ / RSP | 同意/拒绝好友 |
| 10011/10012 | NOTIFY_AUTH_FRIEND_REQ / RSP | 申请方收到结果 |
| 10013/10014 | TEXT_CHAT_MSG_REQ / RSP | 发送文本消息 |
| 10015/10016 | NOTIFY_TEXT_CHAT_MSG_REQ / RSP | 对端收到消息 |
| 99999 | CHAT_HEARTBEAT | 心跳包 |

### 5.3 gRPC（服务间）

消息定义：`Servers/common/proto/message.proto`，三个服务共用一个 proto：

| 服务 | 方法 | 用途 |
|------|------|------|
| VerifyService | `GetVerifyCode` | 生成并发送验证码 |
| StatusService | `GetChatServer` / `Login` | 分配节点+发token / token校验 |
| ChatService | `NotifyAddFriend` / `ReplyFriend` / `SendChatMsg` / `NotifyFriendAccepted` / `NotifyTextChatMsg` / `NotifyKickUser` | 跨服推送/通知 |

---

## 六、核心业务链路

### 6.1 两段式登录（系统最关键设计）

```
Client            Gate         Status        Redis        Chat
  │  POST /userLogin {name, passwd}
  ├───────────────▶│  CheckPassword (MySQL)
  │                ├─ Get(userver_uid)          # 防重登：已在线则拒绝
  │                ├──── GetChatServer(uid) ─▶ 选最空闲节点
  │                │                            Set(utoken_uid = UUID)
  │                │◀─── {host, port, token} ───┤
  │◀── {uid, token, host, port} ─┤
  │
  │  ② TCP 建连 + CHAT_LOGIN_REQ {uid, token}
  ├────────────────────────────────────────────▶│ Get(utoken_uid) 校验
  │                                             │ Get(uinfo_uid)  # miss 回源 MySQL
  │                                             │ HIncrBy(login_count, +1)
  │                                             │ Set(userver_uid = 本节点)
  │◀── CHAT_LOGIN_RSP {资料 + apply_list + friend_list} ─┤
```

**token 全生命周期**：
1. **颁发**：StatusServer 生成 UUID → 写 Redis → 随响应返回客户端。
2. **二次校验**：客户端 TCP 带 token，ChatServer 读 Redis 比对（无值→`UID_INVALID`，不匹配→`TOKEN_INVALID`）。
3. **失效**：会话断开时删除，token 只服务本次登录。

**防重复登录（两道防线）**：
- **入口拦截**：Gate 查 `userver_{uid}`，已在线返回 `USER_ALREADY_LOGIN(10)`，不发新 token。
- **会话顶替保护**：Chat 侧 `RmvUserSession` 做 session 身份比对。

### 6.2 加好友与认证

```
A 搜索 B → ADD_FRIEND_REQ
   → Chat_A: 落库 friend_apply
      → 查 userver_B 路由
         ├─ 同服: 直接推 NOTIFY_ADD_FRIEND_REQ
         └─ 跨服: gRPC NotifyAddFriend → Chat_B 推送
B 收到 → 同意(AUTH_FRIEND_REQ, action=1) / 拒绝(action=2)
   → Chat_B: 更新申请状态 → 同意则双向写 friend 表
      → 通知 A（同服/跨服）
```

### 6.3 跨服文本聊天

```
A(在Chat1) → TEXT_CHAT_MSG_REQ（text_array 批量）
   → Chat1.HandleTextChatMsg
      → Redis.Get("userver_"+B) → "ChatServer2"
         ├─ 同服: 直接推 NOTIFY_TEXT_CHAT_MSG_REQ
         └─ 跨服: ChatGrpcClient.NotifyTextChatMsg ─gRPC─▶ Chat2
                     → Chat2.UserManager.GetSession(B).SendMessage(...)
                        → B 收到推送
```

---

## 七、数据模型

### 7.1 MySQL 表

| 表 | 用途 |
|----|------|
| `user_id` | 全局发号器（事务内 `UPDATE user_id SET id = LAST_INSERT_ID(id + 1)`） |
| `user` | 用户账号资料（name/email/pwd/nick/icon/sex/desc） |
| `friend_apply` | 好友申请，唯一键 `(from_uid, to_uid)`，status：0待处理 / 1同意 / 2拒绝 |
| `friend` | 好友关系（同意后双向 INSERT） |

### 7.2 Redis 键

| 键 | 类型 | 用途 | 写入方 / 读取方 |
|----|------|------|----------------|
| `code_{email}` | string | 邮箱验证码（TTL 600s） | Verify 写 / Gate 读 |
| `utoken_{uid}` | string | 登录 token（UUID） | Status 写 / Chat 校验 / 退出删 |
| `uinfo_{uid}` | string | 用户资料 JSON 缓存 | Chat 读写（miss 回源 MySQL） |
| `userver_{uid}` | string | 用户所在节点名（跨服路由 + 防重登） | Chat 读写 |
| `login_count` | hash | 各节点在线连接计数 | Chat 增减 / Status 读 |

### 7.3 客户端内存模型

```
UserManager（单例）
 ├─ token_ / userInfo_
 ├─ applyList_ : unordered_map<int32_t, shared_ptr<ApplyInfo>>
 └─ friendMap_ : unordered_map<int32_t, shared_ptr<FriendInfo>>
                                        └─ chat_msgs_（全部历史消息）
```

---

## 八、并发与线程模型

### 8.1 各服务的线程模型

| 服务 | 线程模型 |
|------|----------|
| GateServer | 1 线程 accept + 2 个 io_context 业务线程（round-robin 分配连接） |
| ChatServer | 1 线程 accept + 2 个 io_context I/O 线程 + **1 个业务消费线程** + 1 个 gRPC 线程 |
| StatusServer | 1 个 gRPC 线程 + 1 个后台刷新线程（每秒刷新节点负载） |
| VerifyServer | Node.js 单线程事件循环 |

### 8.2 ChatServer 生产-消费

- **I/O 线程**：读完整帧 → `PostMsgToQue` 入队。
- **消费线程（单线程）**：`DealMessage` 出队 → `ProcessMessage` 分发。**注意：当前在持锁期间直接执行 DB/Redis/gRPC 阻塞调用**（见改进清单 P1-1）。

### 8.3 并发安全要点

- ChatServer session 表用 `weak_ptr` + mutex 保护。
- `CSession::Close()` 用 `compare_exchange_strong` 保证只关一次。
- ChatServer 优雅停机：信号回调只 stop，清理按序在 run() 后执行。

---

## 九、启动与运维

### 9.1 start.sh（启动顺序）

严格按依赖顺序，每步 `pgrep` 幂等检查 + sleep + 复检，失败 `exit 1`：
```
[1/5] Redis          redis-server --daemonize yes
[2/5] VerifyServer   nohup node server.js
[3/5] GateServer     nohup ./gateServer
[4/5] ChatServer     多实例循环：chatServer config.ini / config2.ini
[5/5] StatusServer   nohup ./statusServer
```

### 9.2 stop.sh（优雅停机）

**反序停机**（先切流量）：
```
[1/4] GateServer   最多等 60s 优雅退出，超时 pkill -9
[2/4] ChatServer   多实例，最多等 60s（停 IO 池+关 gRPC+清 Redis+析构）
[3/4] StatusServer
[4/4] VerifyServer
```
> Redis 不在 stop 中关闭（设计为复用基础设施）。

### 9.3 status.sh

逐服务 `pgrep` 显示 PID/端口/内存，检查 Redis 与 MySQL 连通性，列出日志文件。

### 9.4 客户端启动

```
cd Client
cmake -S . -B build
cmake --build build -j1
./build/ChatClient
```
> 默认连接 `http://81.69.247.52:9090`（写死在 `Client/src/utils.cpp`），本地部署需改 `SERVER_HOST`。

---

## 十、设计亮点小结

- ✅ **公共静态库 + 瘦服务**：三服务共享 `libchat_common.a`。
- ✅ **最小连接数负载均衡**：后台算好 + 请求读缓存 + 两阶段锁。
- ✅ **MySQL 连接池成熟处理**：超时、健康检查、事务兜底、防 `Commands out of sync`。
- ✅ **会话顶替竞态处理**：session 身份比对。
- ✅ **`Defer` 统一响应写回**。
- ✅ **DAO 全 PreparedStatement**：无 SQL 注入。
- ✅ **客户端五层解耦**：边界清晰，多播分发干净。
- ✅ **收包状态机 + 防洪**：客户端 1MB/10MB 两道防线。

> 已知限制与改进方向详见 `chat项目改进建议汇总.md`。
