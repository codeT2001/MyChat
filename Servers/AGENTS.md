# AGENTS.md

本文件为在本仓库中工作的 AI Agent 提供指引。**与用户交流、代码注释、提交说明及新增文档统一使用中文。**

## 项目概览

即时通讯（IM）系统的服务端集群。`Servers/`（本目录）即构建入口，包含三个 C++17 服务、一个公共静态库和一个 Node.js 验证服务：

| 目录 | 角色 | 技术 | 端口 |
|---|---|---|---|
| `common/` | 公共静态库 `libchat_common.a` | C++17 / gRPC / Boost / hiredis+redis++ / mysqlcppconn / jsoncpp | — |
| `GateServer/` | HTTP 网关：注册 / 登录 / 验证码 / 改密 | Boost.Beast | HTTP 9090 |
| `ChatServer/` | TCP 聊天节点（可多实例） | Boost.Asio + gRPC | TCP 50061/50062，gRPC 50081/50082 |
| `StatusServer/` | gRPC 协调：分配节点、颁发 / 校验 token | gRPC | 50052 |
| `VerifyServer/` | 邮箱验证码 | Node.js + gRPC | 50051 |

数据层：Redis（token / 验证码 / 用户资料 / 路由 / 在线计数）、MySQL（`user` / `user_id` / `friend_apply` / `friend`）。

> ⚠️ Qt 客户端位于仓库同级目录 `../Client`，**不属于本目录范围**，请勿在此修改。

## 构建与运行

> 必须在 `Servers/` 下构建；仓库根目录没有 `CMakeLists.txt`。

```bash
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)
# 单目标：cmake --build build --target chatServer -j$(nproc)
```

产物落在 `build/<Server>/`，可执行文件与 `config.ini` 同目录（`config_manager` 依赖相对路径 `./config.ini`，因此**不能**统一输出目录）。

运维脚本（启动顺序 Redis → VerifyServer → GateServer → ChatServer1/2 → StatusServer）：

```bash
./start.sh    # 启动全部服务
./status.sh   # 查看 PID / 端口 / 内存
./stop.sh     # 反序优雅停止（不停 Redis/MySQL）
```

- 无自动化测试、无 lint 配置；改动依靠编译 + 手工回归验证。
- 验证编译：`cmake --build build --target <gateServer|chatServer|statusServer> -j1`。

## 代码约定

- 公共库符号统一在命名空间 `P1`；头文件以 `#include "chat/xxx.h"` 引用。
- 业务服务只需 `target_link_libraries(<server> PRIVATE chat_common)`，外部依赖由 `common` 以 `PUBLIC` 传递。
- 日志统一用 `LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERROR`（`common/include/chat/log.h`）；**禁止 `std::cout`/`std::cerr`/`printf`**；含 token/密码的请求禁止打印整个 body。
- HTTP / TCP Handler 回包统一用 `Defer` 兜底；新增错误分支直接 `return`，不要漏发响应。
- 错误码统一使用 `common/include/chat/constants.h` 的 `ErrorCodes`；Node.js `VerifyServer` 另有一套独立编号（见 `VerifyServer/README.md`）。
- `proto` 唯一源文件是 `common/proto/message.proto`；生成物在 `build/common/grpc_gen/`，**不入库、勿手改**。

## 关键约束与易踩坑

- **TCP 帧**：`2 字节 msgId + 2 字节长度 + JSON`（大端）；单帧 body 上限 `MAX_MSG_LENGTH = 2048`，超限直接断连。
- **业务串行**：ChatServer 所有 Handler 跑在同一逻辑线程，禁止长时间阻塞；跨服 gRPC 为同步调用且未设 deadline。
- **必须存在 `user_id` 发号器表**，否则注册事务（`LAST_INSERT_ID(id+1)`）失败。
- **Redis 键无 TTL**：`utoken_{uid}` / `userver_{uid}` 在进程异常退出时会残留，靠下次登录顶号流程覆盖自愈。
- 顶号模式：重复登录不拒绝，由 ChatServer 踢掉旧会话（同节点 `NOTIFY_KICK`，跨节点 `ChatService.NotifyKickUser`）。
- 真实配置（`config.ini`、`config2.ini`、`config.json`、`.env`）已被 `.gitignore` 忽略，仓库只保留 `*.example`；**不要提交真实凭据**。
- 固定端口：9090 / 50051 / 50052 / 50061 / 50062 / 50081 / 50082 / 6379 / 3306。

## 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 架构总览、端口、核心流程、接口契约、错误码、故障排除 |
| `DEPLOY.md` | 依赖安装、数据库初始化、构建、启动冒烟、systemd |
| `REFACTORING.md` | 三个 server 解耦的设计决策与权衡 |
| `common/README.md` | 公共库组件、proto 契约、Redis 键约定、基础设施行为 |
| `{GateServer,ChatServer,StatusServer,VerifyServer}/README.md` | 各服务内部流程与时序图 |
