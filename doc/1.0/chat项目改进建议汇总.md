# Chat 分布式 IM 系统 · 问题与改进汇总清单

> 本文是**唯一权威问题清单**，合并去重了此前所有分析（代码走读 + 网络专项）。
> 每条含 **编号 / 文件:行号 / 问题 / 影响 / 建议**，按优先级分组。
> 配套文档：`chat架构与逻辑详述.md`（架构说明）、`chat网络传输与中间件改进.md`（网络专项展开）。

---

## 目录

- [问题总览](#问题总览一图看全)
- [P0 安全类（必修）](#p0--安全类必修)
- [P1 架构与并发](#p1--架构与并发)
- [P2 网络与可靠性](#p2--网络与可靠性)
- [P3 功能缺陷](#p3--功能缺陷)
- [P4 客户端工程问题](#p4--客户端工程问题)
- [P5 工程规范与可维护性](#p5--工程规范与可维护性)
- [值得保持的设计](#值得保持的设计)
- [分阶段落地路线](#分阶段落地路线)

---

## 问题总览（一图看全）

| 级别 | 类别 | 条数 | 核心风险 |
|------|------|------|----------|
| **P0** | 安全 | 6 | 凭据泄漏、密码明文、传输无加密 |
| **P1** | 架构与并发 | 6 | 级联阻塞、吞吐瓶颈、死锁 |
| **P2** | 网络与可靠性 | 8 | 消息丢失、帧截断、跨服阻塞 |
| **P3** | 功能缺陷 | 6 | 客户端可用性、离线消息丢失 |
| **P4** | 客户端工程 | 6 | 编译失败、UB、UI 挂死 |
| **P5** | 工程规范 | 7 | 可维护性、环境一致性、无测试 |
| **合计** | | **39** | |

---

## P0 · 安全类（必修）

### ✅ P0-1 明文凭据提交入库（代码侧已修复，凭据轮换/历史清理待人工执行）

- **位置**：`Servers/VerifyServer/config.json:3-4`、各 `config.ini`（MySQL 段）、`Servers/status.sh`
- **问题**：QQ 邮箱账号 + **授权码**明文提交；MySQL 用户名/口令在多处配置中明文。
- **影响**：拿到仓库即可发信、连库，属**实质泄露**。
- **已落地修复**（真实配置不入库 + 仓库只留 `.example` 假值模板；shell 脚本凭据走 `.env`，服务配置保持本地明文，改动最小）：
  1. 三个 C++ 服务的 `config.ini` / `config2.ini` / `VerifyServer/config.json` 已 `git rm --cached` 移出跟踪（本地明文文件保留，服务照常运行），并在 `Servers/.gitignore` 中忽略；仓库仅保留 `config.ini.example`、`config2.ini.example`、`config.json.example`，模板内只放假值；
  2. 新增 `Servers/.env`（脚本凭据，已被忽略，建议 `chmod 600`）+ `.env.example` 模板；**仅 shell 脚本使用**：`status.sh` 的 MySQL 探活改读 `.env` 中 `MYSQL_HOST/MYSQL_PORT/MYSQL_USER/MYSQL_PASSWORD`，未配置则跳过，不再硬编码；`start.sh` 存在 `.env` 时加载导出、缺失仅告警不阻断（服务自身不依赖环境变量）；
  3. 服务侧配置读取逻辑基本不变：`ConfigManager` 仍按普通 ini 读取（本地文件可直接写明文），新增敏感键（pwd/pass/token/key 等）日志自动脱敏，避免口令进入日志；VerifyServer `config.js` 直接读本地 `config.json`；
  4. CMake 在缺少真实 ini 时自动回退复制 `.example`，全新 clone 仍可编译（运行前需自行复制并填写真实配置）；
  5. `DEPLOY.md` / `Servers/README.md` 中的建权 SQL、探活命令同步改为占位符与 `.env` 方式，`test-smtp.js` 发件人改取配置不再硬编码邮箱。
- **仍需人工处理**：
  1. **立即轮换凭据**：QQ 邮箱授权码与 MySQL 口令已进 git 历史，必须视为已泄露；轮换后更新部署机各 `config.ini` / `config.json` 与 `Servers/.env`；
  2. **清理 git 历史**：安装 `git-filter-repo`（`pip install git-filter-repo`）后重写历史并强推，协作方需重新 clone；
  3. 各部署机首次部署执行 `cp .env.example .env`（脚本用）、按需 `cp config.ini.example config.ini` / `cp config.json.example config.json` 并填入真实值。

### 🔴 P0-2 密码明文存储与传输

- **位置**：`Servers/common/src/mysql_dao.cpp:411`（CheckPassword）、`:375`（UpdatePassword）、`GateServer/src/logic_system.cpp:123`
- **问题**：`WHERE name=? AND pwd=?` 直接比明文；注册/改密写明文；登录成功还把 pwd 回传客户端。
- **影响**：库泄漏即全量泄露；响应体含密码。
- **建议**：① 加盐哈希（bcrypt/argon2id）+ 恒定时间比较；② 响应体永不返回 pwd。

### 🔴 P0-3 传输层全程无加密

- **位置**：`Client/src/utils.cpp:11-12`、`Servers/common/include/chat/rpc_stub_pool.h:25`、各 gRPC server
- **问题**：HTTP / TCP / gRPC 全部明文（Insecure）。
- **影响**：公网传输 token、密码、聊天内容可被窃听/篡改。
- **建议**：① HTTP→HTTPS；② TCP/WS→TLS；③ gRPC 启用 TLS。📎 详见网络专项文档。

### 🟠 P0-4 验证码强度弱且无尝试限制

- **位置**：`Servers/VerifyServer/server.js:28-30`；校验方 `logic_system.cpp:128,189`
- **问题**：验证码只取 UUID 前 4 位（约 65536 空间）；无失败次数限制/锁定。
- **影响**：可暴力枚举。
- **建议**：① 改 6 位数字；② 加尝试计数与发送频率限制。

### 🟡 P0-5 字段校验不一致（边界安全）

- **位置**：`Servers/GateServer/src/logic_system.cpp:196`（`HandleResetPassword` 未校验 name）
- **问题**：与注册流程校验不一致，恶意 JSON 触发 JsonCpp 默认值行为。
- **建议**：抽出统一字段校验辅助函数，所有 handler 复用。
- **备注**：DAO 层已全用 PreparedStatement，**无 SQL 注入风险**（这点做得好）。

### 🟡 P0-6 敏感信息记录/外泄

- **位置**：`GateServer/src/logic_system.cpp:246` 等日志
- **问题**：日志含用户名等；对外错误码未统一映射（见 P4-6）。
- **建议**：日志脱敏；对外错误码与内部错误分离。

---

## P1 · 架构与并发

### 🔴 P1-1 业务单线程 + 阻塞 IO = 级联阻塞

- **位置**：`Servers/ChatServer/src/logic_system.cpp:40-66`（`DealMessage`）
- **问题**：所有消息由**单线程**消费，且在持锁期间直接执行 DB/Redis/gRPC **同步阻塞调用**。
- **影响**：一次慢调用卡住所有用户全部消息。
- **建议**：① 阻塞 IO 移出消费线程；② 或多消费者线程按 uid 哈希分片；③ 缩小锁粒度。

### 🔴 P1-2 gRPC stub 池 GetStub 无超时

- **位置**：`Servers/common/include/chat/rpc_stub_pool.h:60`
- **问题**：`cond_.wait` 无超时（对比 MySQL 池用 `wait_for(3s)`），池耗尽后所有线程永久阻塞。
- **建议**：① 改 `wait_for` 带超时快速失败；② 给所有 RPC 设 deadline；③ 加池耗尽告警。

### 🟠 P1-3 gRPC 调用无 deadline

- **位置**：`Servers/ChatServer/src/chat_grpc_client.cpp`、`common/src/status_grpc_client.cpp`、`common/src/verify_grpc_client.cpp`
- **问题**：跨服调用同步且无 deadline，网络抖动时长时间挂起。
- **建议**：为每次 RPC 设置 1-3s deadline + 重试/快速失败。

### 🟠 P1-4 Redis 读操作无异常兜底

- **位置**：`Servers/common/src/redis_manager.cpp`（Get/Set/HSet 直接调用 client）
- **问题**：Redis 抖动抛 `sw::redis::Error`；ChatServer 有 try/catch 兜住，**GateServer 无**，会中断请求。
- **建议**：① 各方法内 try/catch 返回 `nullopt`；② GateServer 加统一异常兜底。

### 🟡 P1-5 LogicSystem::stop_ 非原子

- **位置**：`Servers/ChatServer/src/logic_system.h:46`
- **问题**：普通 `bool`，跨线程读写属数据竞争（UB）。对比 `StatusServiceImpl::running_` 用了 atomic。
- **建议**：改 `std::atomic<bool>`。

### 🟡 P1-6 客户端 unordered_map 分页顺序不稳定

- **位置**：`Client/src/usermanager.cpp:118-124`
- **问题**：在 `unordered_map` 上用 `std::advance` 取第 N 页，顺序不稳定，插入时可能重复/漏项。
- **建议**：改有序容器或按时间排序后再分页。

---

## P2 · 网络与可靠性

> 📎 本节 P2-3/P2-5/P2-6 的**方案展开**（Protobuf / RabbitMQ / Redis Stream / WebSocket 选型）见 `chat网络传输与中间件改进.md`。

### 🟠 P2-1 TCP 单帧 uint16 截断无校验

- **位置**：`Servers/ChatServer/src/csession.cpp:161`（发送）；`:85`（接收校验）
- **问题**：长度字段 `uint16_t` 硬上限 65535，发送侧强转却未校验上限；客户端/服务端 `MAX_MSG_LENGTH` 定义不一致（服务端 2048）。
- **建议**：① 发送前断言上限，超限走分片；② 统一常量（单一来源）。

### 🟠 P2-2 发送队列满静默丢消息

- **位置**：`Servers/ChatServer/src/csession.cpp:176-179`
- **问题**：队列 ≥2000 时直接 return 丢消息，无回执、无背压。
- **建议**：① 队列满回执/限流（背压）；② 关键消息本地暂存+重试。

### 🔴 P2-3 消息不落库、离线不补发

- **位置**：`Servers/ChatServer/src/chat_service_impl.cpp:26,60,102`（三处 TODO）
- **问题**：对端不在线消息静默丢弃；聊天记录仅存客户端内存，重启即丢。
- **建议**：① 增加消息表落库；② 登录拉取离线消息；③ 已读/送达回执闭环。

### 🟠 P2-4 心跳无超时检测

- **位置**：`Servers/ChatServer/src/csession.cpp`、`Client/src/tcpmanager.cpp:168-175`
- **问题**：只发不检测；半开连接（拔网线）在 TCP keepalive 前无法感知。
- **建议**：两端维护 `last_active`，超时判死链并主动断开。

### 🟠 P2-5 跨服转发同步阻塞 + 拓扑耦合

- **位置**：`Servers/ChatServer/src/chat_grpc_client.cpp:65` 等
- **问题**：跨服用**同步阻塞 gRPC 直连**；节点数增加需维护 O(N²) 连接、改 `PeerServer` 配置重启。
- **建议**：改 **Redis Stream**（复用现有 Redis）做异步消息总线，兼得解耦 + 削峰 + 不丢。📎 见专项文档。

### 🟡 P2-6 Gate 未处理非 GET/POST 方法

- **位置**：`Servers/GateServer/src/http_connection.cpp:132-157`
- **问题**：其他方法（PUT/DELETE/HEAD）既不响应也不取消，挂到 60s 超时。
- **建议**：返回 `405` 并正常关闭。

### 🟡 P2-7 RefreshCache 的 std::stoi 无保护

- **位置**：`Servers/StatusServer/src/status_service_impl.cpp:172`
- **问题**：`LOGIN_COUNT` 值非数字时抛异常，导致本轮刷新整体失败。
- **建议**：用 `std::from_chars` 安全解析，异常按默认值+告警。

### 🟠 P2-8 StatusServer 信号线程捕获栈引用

- **位置**：`Servers/StatusServer/main.cpp:45`
- **问题**：`std::thread([&io_context]{...}).detach()` 捕获栈引用，提前返回则悬垂。
- **建议**：按值/`shared_ptr` 捕获，或明确 join 顺序。

---

## P3 · 功能缺陷

### 🟠 P3-1 图片/文件消息从未发送网络

- **位置**：`Client/src/chatpage.cpp:193-198`
- **问题**：图片只本地建气泡，file 直接 skip，对方永远收不到。
- **建议**：实现文件上传（对象存储/HTTP 分片）+ 消息携带 URL。

### 🟡 P3-2 送达回执未闭环

- **位置**：`Client/src/chatservice.cpp:22-31`（TODO）
- **问题**：气泡无"已送达/失败"态；发送失败无 UI 反馈。
- **建议**：ack 后更新气泡状态；失败可重发。

### 🟡 P3-3 InfoPage 三个"死按钮"

- **位置**：`Client/src/infopage.cpp:53-55`
- **问题**：`SigSendMessage/SigVoiceCall/SigVideoCall` 无任何 connect，点击无效。
- **建议**：补信号槽；未实现的语音/视频先隐藏置灰。

### 🟡 P3-4 搜索非数字输入无提示

- **位置**：`Client/src/searchlist.cpp:93-94`
- **问题**：仅 `LOG_WARN`，用户无感知。
- **建议**：UI 提示"请输入数字 UID"。

### 🟡 P3-5 注册倒计时多 1 秒

- **位置**：`Client/src/registerdialog.cpp:85-96` + `:219-227`
- **问题**：减到 0 仍显示"0 s"，第 6 次才切换，实际 6 秒。
- **建议**：调整判零时机或初始值。

### 🟡 P3-6 好友备注/标签不同步服务端

- **位置**：`Client/src/infopage.cpp:183-199`、`usermanager.cpp:210-228`
- **问题**：注释明确"服务端同步协议待接入"。
- **建议**：新增同步协议与落库。

---

## P4 · 客户端工程问题

### 🔴 P4-1 include 大小写不匹配 → Linux 必编译失败

- **位置**：`Client/src/chatuserlist.cpp:1`（`#include "ChatUserList.h"`），实际文件名 `inc/chatuserlist.h`（小写，已字节级核实）
- **问题**：Windows/macOS 能编过，**Linux CI 直接失败**。
- **建议**：改小写 include；CI 加 Linux 构建。

### 🟠 P4-2 未初始化成员（UB）

- **位置**：`Client/inc/usermanager.h:61-62`（`int chatLoaded_; int contactLoaded_;`），构造函数为空
- **问题**：读取未初始化值是 UB，碰巧为 0 才正常。
- **建议**：类内初始化 `= 0`。

### 🔴 P4-3 重连逻辑自相矛盾 → 退避失效

- **位置**：`Client/src/tcpmanager.cpp:198-218`
- **问题**：先起退避定时器又**立即** `connectToHost`，退避形同虚设；与 `disconnected` 回调形成双路径，计数递增失控。
- **建议**：统一重连状态机（定时器只负责延迟连接，或去掉定时器）。

### 🔴 P4-4 TCP 连接失败 UI 挂死、无反馈

- **位置**：`Client/src/authservice.cpp:69-78`、`tcpmanager.cpp:82-86,191`
- **问题**：只处理成功分支；`SigReconnectFailed` **全工程无监听**。ChatServer 不可达时登录按钮永久禁用无提示。
- **建议**：`errorOccurred` 发失败信号 → `AuthService` 恢复 UI+提示；监听 `SigReconnectFailed`。

### 🟡 P4-5 客户端心跳无超时判定

- 同 [P2-4](#-p2-4-心跳无超时检测)，客户端只发不检测。
- **建议**：维护 PONG 超时，连续 N 次未响应判死链重连。

### 🟡 P4-6 错误提示粒度粗 + 命名/遗留

- **位置**：`Client/src/authservice.cpp:26`（统一"参数错误"）；`constants.h:65-68`（`MSG_IDS` 与 `RequestId` 重复）；`tcpmanager.cpp:130-133`（`InitHandlers()` 空函数）
- **建议**：① 按 `ErrorCodes` 映射文案；② 清理死代码；③ 修正拼写（`recvPedding_`→`pending`、`cancle`）。

---

## P5 · 工程规范与可维护性

| 编号 | 问题 | 位置 | 建议 |
|------|------|------|------|
| P5-1 | IO 线程数写死 | `asio_io_context_pool.h:27`（size=2） | 按核数/配置动态 |
| P5-2 | 环境配置不一致 | `StatusServer/config.ini:22` vs `ChatServer/config.ini:26` | 环境变量注入，区分 profile |
| P5-3 | 端口/地址硬编码 | `VerifyServer/server.js:71`、`Client/src/utils.cpp:11` | 全部走配置 |
| P5-4 | 遗留调试文件 | `VerifyServer/test-smtp.js`（依赖未声明的 express） | 删除或补依赖 |
| P5-5 | 热重载加锁被注释 | `config_manager.cpp:32` | 启用热重载时恢复锁 |
| P5-6 | 无测试、无 CI | 全工程 | 单测+集成测试+CI（Linux 构建/静态检查） |
| P5-7 | 协议常量双份维护 | 客户端/服务端各一份 | 单一来源（IDL 生成/共享头） |

---

## 值得保持的设计

改进之外，这些设计做得好，建议保留：

- ✅ **公共静态库 + 瘦服务**：三 C++ 服务共享 `libchat_common.a`（`REFACTORING.md` 有完整权衡记录）。
- ✅ **最小连接数负载均衡**：后台算好 + 请求读缓存 + 两阶段锁（共享锁读快照、独占锁写回），锁竞争极低。
- ✅ **MySQL 连接池成熟处理**：`wait_for(3s)` 超时、空闲健康检查（`SELECT 1` 防 `Commands out of sync`）、事务兜底回滚、损坏连接丢弃、ID 发号技巧。
- ✅ **会话顶替竞态处理**：`RmvUserSession` 做 session 身份比对。
- ✅ **`Defer` 统一响应写回**：避免多分支漏写。
- ✅ **DAO 全 PreparedStatement**：无 SQL 注入。
- ✅ **客户端五层解耦**：边界清晰，多播分发干净。
- ✅ **收包状态机 + 防洪**：客户端 1MB/10MB 两道防线，服务端 bodySize 越界校验。
- ✅ **优雅停机**：信号回调只做最轻的事，清理按序执行；后台定时用"小步睡眠"支持快速退出。

---

## 分阶段落地路线

### 第一阶段：止血（1-2 天）
1. **轮换并移除所有明文凭据**（P0-1）。——代码侧已完成（真实配置移出 git 跟踪、仓库只留 `.example` 假值模板、脚本凭据改读 `.env`、敏感日志脱敏），⚠️ 凭据轮换与 git 历史清理仍需人工执行。
2. 修复 **Linux 编译失败**（P4-1）、**重连/连接失败 UI 问题**（P4-3、P4-4）。
3. gRPC `GetStub` 加超时 + 调用加 deadline（P1-2、P1-3）。

### 第二阶段：安全与稳定（1-2 周）
4. 密码 **bcrypt 哈希**（P0-2），响应体去 pwd。
5. 全链路 **TLS**（P0-3）。
6. 验证码强化 + 频率限制（P0-4）。
7. 业务线程**异步化/多消费者**，阻塞 IO 移出消费线程（P1-1）。

### 第三阶段：网络与可靠性（2-4 周）
8. **跨服转发改 Redis Stream**（P2-5），兼解 P2-2/P2-3。
9. **消息落库 + 离线消息**（P2-3）。
10. **心跳超时检测**（P2-4）+ **背压/丢消息回执**（P2-2）。
11. 序列化统一：TCP 帧换 Protobuf（📎 网络专项文档）。
12. 图片/文件消息网络化（P3-1）、送达回执闭环（P3-2）。

### 第四阶段：工程化（持续）
13. 补单元/集成测试与 **CI**（P5-6）。
14. 协议常量单一来源、配置分环境、清理死代码（P5-7、P5-2、P5-4）。
15. 客户端低级问题批量修复（P4-2、P4-6、P3-3~P3-6）。

---

## 优先级速查（Top 3 必修）

| 排序 | 问题 | 为什么最急 |
|------|------|-----------|
| 1 | **明文凭据 + 密码明文**（P0-1、P0-2） | 已实质泄露，且是安全红线（P0-1 代码侧已修复，剩凭据轮换/历史清理） |
| 2 | **业务单线程 + gRPC 无超时**（P1-1、P1-2） | 一次慢调用卡死全服（架构级风险） |
| 3 | **Linux 编译失败 + 连接失败 UI 挂死**（P4-1、P4-4） | 直接影响可用性与交付 |

> 说明：以上为改进建议清单，不含对原作者的评判。项目作为分布式 IM 学习/实战范例，骨架（两段式登录、跨服路由、负载均衡、多节点扩展）已相当扎实，主要短板集中在**安全、并发模型、可靠性**三块。
