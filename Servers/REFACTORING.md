# 三 Server 解耦设计思路

> 记录 ChatServer / GateServer / StatusServer 三个 C++ 项目解耦改造的设计思考与决策依据。

## 目录

- [一、问题诊断](#一问题诊断)
- [二、方案选型](#二方案选型)
- [三、关键设计决策](#三关键设计决策)
- [四、架构权衡](#四架构权衡)
- [五、后续演进方向](#五后续演进方向)
- [六、参考资料](#六参考资料)

---

## 一、问题诊断

### 1.1 重复代码的层次

三个 server 的代码可分两层：

- **基础设施层**：单例、配置、Redis、MySQL、gRPC 客户端、proto、错误码 —— 几乎完全重复
- **业务层**：TCP/HTTP/gRPC server 实现、路由、消息处理 —— 各自不同

改造前所有基础设施代码在三个 server 各有一份独立拷贝。这种"复制粘贴式复用"带来三个核心问题：

1. **同步漂移**：改一处要同步改三处，极易遗漏
2. **约定不一致**：三个 `constants.h` 中 `ErrorCodes` 枚举值已经出现冲突（`TOKEN_INVALID` 在 StatusServer 是 8、在 ChatServer 是 9），且没人发现
3. **proto 三份独立维护**：源文件三份拷贝，生成代码也三份手 commit，源与生成物都容易脱节

### 1.2 痛点排序

按"重复度 × 风险"排序，最值得抽的是：

1. `redis_manager` / `config_manager` / `rpc_stub_pool`（3/3 一致）
2. `mysql_dao` / `status_grpc_client` 等只 Chat/Gate 共享的（2/2 一致或微差）
3. `constants.h`（3/3 不一致，是 bug 温床）
4. proto 与生成代码（3/3 一致但易脱节）

---

## 二、方案选型

考虑过四种解耦方式：

| 方案 | 思路 | 优点 | 缺点 |
|---|---|---|---|
| **A. 静态库 `common/`** | 抽公共代码到 `libchat_common.a`，server link 即用 | 一份生效三处用、CMake 原生支持、改动可控 | 需建顶层 CMake 项目 |
| B. Header-Only | 全部用 header-only | 零链接产物 | 带状态的实现（连接池、单例静态变量）会三处实例化，违背单例语义 |
| C. 微服务化 | DB/Redis 拆成独立进程，server 通过 RPC 调 | 彻底解耦、连接池统一 | 过度设计：原本进程内调用变 RPC，延迟显著上升，引入新单点 |
| D. Git Submodule | 公共代码独立仓，submodule 引入 | 不改 CMake 结构 | 仍各自编译、不节省编译时间；submodule 更新麻烦 |

**最终选择**：A + B 混合 —— 纯模板/宏（`noncopyable.h`、`rpc_stub_pool.h`）用 header-only；带状态的实现（`redis_manager`、`mysql_dao` 等）编入静态库。

**为什么不用 C**：当前 QPS 和团队规模不值得引入跨进程 RPC 的延迟与故障成本。"内部函数调用"远比"网络往返"简单可靠。

---

## 三、关键设计决策

### 3.1 为什么 common 是静态库而不是 header-only

带状态的代码（连接池、单例）若做成 header-only，会在每个翻译单元里展开，单例的 `static` 变量也会出现多个实例，违背单例语义。静态库把状态收敛到一份 `.o`，是唯一正确选择。

### 3.2 为什么 proto 提到 `common/proto/`

common 内的 `status_grpc_client.cpp` 编译时需要 `message.grpc.pb.h`。如果不把 proto 收归 common，common 就要反向依赖某个 server 的 `grpc_gen/`，形成循环依赖。proto 收归后，common 自给自足，server 只依赖 common。

**附带收益**：proto 生成代码用 `add_custom_command` 自动生成到 build 目录，不入源码树，源与生成物不再可能脱节。

### 3.3 为什么 constants.h 也合并

用户初版方案是"constants 暂不合并"。但发现 common 的 `status_grpc_client.cpp` 用了 `ErrorCodes::PRC_FAILED`，必须依赖 constants。三种选择：

- 让 common 链接某个 server 的 `constants.h` —— 破坏解耦，且只能选一个 server
- common 用魔法数字 `2` 替代 —— 维护噩梦
- 把 ErrorCodes 提到 common —— 一劳永逸

`ErrorCodes` 本质是 **gRPC 协议契约**（client 与 server 共享的整数约定），属于基础设施而非业务常量。合并它符合"基础设施统一"的目标。其他业务常量（如 `MSG_IDS`、`USER_TOKEN_PREFIX`）顺势也合进来，因为它们也跨 server 共享。

### 3.4 为什么 `UID_INVALID=8, TOKEN_INVALID=9`（而非 StatusServer 的反向顺序）

三个 server 原本的枚举值已经不一致。必须选一个权威顺序。选择依据：

- ChatServer 顺序：`UID_INVALID=8, TOKEN_INVALID=9`
- StatusServer 顺序：`TOKEN_INVALID=8, UID_INVALID=9`

**安全性验证**：

- GateServer（gRPC 客户端）只用 `if (reply.error())` 检查真值，不比较具体整数 —— 不受影响
- ChatClient 的 `ErrorCodes` 根本没有 `UID_INVALID`/`TOKEN_INVALID` —— 不受影响
- 没有任何代码把这两个枚举值通过 wire protocol 发给客户端

所以**改值不会破坏 wire protocol**，可以放心统一。

### 3.5 为什么用 `constexpr char[]` 替代 `#define`

原 `USERTOKENPREFIX` 是宏，`TOKEN_KEY_PREFIX` 是另一个名字的本地 constexpr。统一为 `USER_TOKEN_PREFIX` 用 `constexpr char[]`：

- 类型安全（`const char*` 而非无类型替换）
- 作用域受限（不像宏会污染所有翻译单元）
- 现代 C++ 风格

### 3.6 为什么不统一 `CMAKE_RUNTIME_OUTPUT_DIRECTORY`

第一版把所有可执行文件输出到 `build/bin/`，结果发现三个 server 的 `config.ini` 都复制到同一 `bin/config.ini`，最后一个覆盖前面的。

`config_manager.cpp` 用相对路径 `"./config.ini"` 加载配置，依赖"可执行文件与 config.ini 同目录"。如果强行统一到 `bin/`，要么三个 config.ini 互相覆盖（错误），要么改 config_manager 接受 server 名作参数（过度改动）。

**最简方案**：放弃统一输出目录，让每个 server 落在自己的 `build/<server>/` 子目录，可执行文件与 config.ini 自然同处。代价是"可执行文件不在同一目录"，但 `start.sh` 已通过 `cd $PROJECT_DIR/build/<server>` 适配，运行不影响。

### 3.7 为什么 common 的依赖用 PUBLIC 传递

静态库的链接依赖默认不传递。如果用 `target_link_libraries(chat_common PRIVATE ...)`，server 链接 chat_common 时拿不到 gRPC/Boost 等符号，会报 undefined reference。

用 `PUBLIC` 让依赖传递：server CMakeLists 只需写 `target_link_libraries(myserver PRIVATE chat_common)` 一行，所有外部依赖自动到位。代价是 server 被迫接受 common 选定的 gRPC/Boost 版本 —— 但这正是我们想要的"统一约定"。

### 3.8 为什么保留 ChatClient 不动

用户明确范围是"三个 Server"。ChatClient 是 Qt 应用，有独立的 `constants.h`（含 `RequestId` 等 Qt 专属枚举）和 `Singleton<T>` 模板。强行合并会引入 Qt 依赖污染 server。让它独立演进是更干净的选择。

---

## 四、架构权衡

### 4.1 改造了什么 / 没改造什么

| 改了 | 没改 |
|---|---|
| 基础设施代码集中到 `common/` | 业务代码（cserver/csession/logic_system）原位不动 |
| 单例模式从模板继承改为 Meyers' | namespace `P1` 保持不变（避免大面积改名） |
| proto 源文件统一到 `common/proto/` | 各 server 的 `config.ini` 内容不动（端口/DB 凭据不变） |
| 错误码值统一 | gRPC wire protocol 不变（向后兼容） |
| 顶层 CMakeLists.txt 统一构建入口 | ChatClient 仍独立 |
| 启动脚本 `cd` 路径适配新 build 结构 | 日志目录位置保持 `chat/logs/` |

### 4.2 设计的"最小入侵"原则

业务代码改动仅限于：

- `#include "xxx.h"` → `#include "chat/xxx.h"`（include 路径前缀）
- `USERTOKENPREFIX` → `USER_TOKEN_PREFIX`（仅 StatusServer 2 处）
- `TOKEN_KEY_PREFIX` → `USER_TOKEN_PREFIX`（仅 ChatServer 2 处，含本地定义删除）
- `auto pool = ...` → `auto& pool = ...`（避免触发已删除的拷贝构造）

业务逻辑、类结构、gRPC 接口均未触碰。

### 4.3 本次改造未解决的问题（遗留）

解耦只解决了"代码重复"，以下问题**依旧存在**，属于下一阶段的工作：

| 遗留问题 | 说明 |
|---|---|
| **单例仍是硬编码依赖** | 本次把"模板单例"换成"Meyers 单例"，但业务代码仍是 `RedisManagerPool::GetInstance()` 直接调用，无法在测试中替换实现。需进一步抽象接口 + 依赖注入 |
| **构造函数内做 IO** | 多个单例在构造时就初始化连接池/配置，导致无法脱离真实 Redis/MySQL 实例化，阻碍单元测试 |
| **错误码会话未统一** | VerifyServer（Node.js）仍用另一套错误码编号，与 C++ `ErrorCodes` 冲突 |
| **无测试** | 本次重构**没有任何自动化测试保护**，仅靠手工回归验证 |

---

## 五、后续演进方向

1. **业务层抽象**：本次只解耦基础设施层。`logic_system` 的路由注册模式、`cserver` 的 TCP/HTTP server 基类还可进一步抽象（如 `BaseServer`、`RouteRegistry`）
2. **接口化 + 依赖注入**：把 `IRedisCache` / `IUserRepository` 等抽成接口，用 Fake 实现替换，让业务逻辑可脱离真实中间件做单元测试（解决 4.3 的前两条）
3. **proto 拆分**：随着服务增多，`message.proto` 可拆为 `verify.proto` / `status.proto` / `chat.proto`，避免单文件膨胀
4. **ChatClient 接入 common**：若需要，可让 Qt 项目 link `chat_common`，复用 `ErrorCodes`、`RedisManager` 等
5. **统一日志框架**：当前依赖 `nohup` 重定向 stdout/stderr。引入 spdlog/glog 可做结构化日志、按级别过滤、自动轮转
6. **补齐测试与 CI**：优先补协议层（帧编解码、负载均衡选点）纯函数单测，再逐步覆盖业务 Handler

---

## 六、参考资料

- 改造前代码状态：见 git history（`singleton.h` 模板继承、三份 `grpc_gen/`、三份 `constants.h`）
- CMake Modern Targets：https://cmake.org/cmake/help/latest/manual/cmake-targets.7.html
- Meyers' Singleton：Effective C++ Item 4 / S. Meyers
- gRPC C++ codegen：https://grpc.io/docs/languages/cpp/basics/
