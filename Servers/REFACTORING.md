# 三 Server 解耦设计思路

> 记录 ChatServer / GateServer / StatusServer 三个 C++ 项目解耦改造的设计思考与决策依据。当前代码结构见 [README.md](README.md) 与 [common/README.md](common/README.md)。

## 一、问题诊断

三个 server 的代码分两层：**基础设施层**（单例、配置、Redis、MySQL、gRPC 客户端、proto、错误码）几乎完全重复；**业务层**（TCP/HTTP/gRPC server 实现、路由、消息处理）各自不同。改造前基础设施代码在三个 server 各有一份拷贝，带来：

1. **同步漂移**：改一处要同步改三处，极易遗漏
2. **约定不一致**：三份 `constants.h` 的 `ErrorCodes` 已冲突（`TOKEN_INVALID` 在 StatusServer=8、ChatServer=9）且无人发现
3. **proto 三份维护**：源与生成代码各三份、手工 commit，容易脱节

按"重复度 × 风险"排序：连接池 / 配置 / `redis_manager`（3/3）> `mysql_dao` 等（2/2）> `constants.h`（3/3 不一致，bug 温床）> proto 与生成物。

## 二、方案选型

| 方案 | 思路 | 结论 |
|---|---|---|
| **A. 静态库 `common/`** | 抽公共代码到 `libchat_common.a`，server link 即用 | **✅ 采用** |
| B. Header-Only | 全部 header-only | 带状态实现（连接池、单例）会多份实例化，违背单例语义 |
| C. 微服务化 | DB/Redis 拆进程走 RPC | 过度设计，进程内调用变网络往返，延迟与故障成本高 |
| D. Git Submodule | 公共代码独立仓 | 仍各自编译、不省编译时间，更新麻烦 |

**最终**：A + B 混合 —— 纯模板/宏（`noncopyable.h`、`rpc_stub_pool.h`）用 header-only；带状态实现（`redis_manager`、`mysql_dao` 等）编入静态库。

## 三、关键设计决策

- **common 用静态库而非 header-only**：带状态代码（连接池、单例）若 header-only 会在每个翻译单元展开，`static` 变量出现多份实例；静态库把状态收敛到一份 `.o`。
- **proto 提到 `common/proto/`**：`status_grpc_client.cpp` 编译需要 `message.grpc.pb.h`，否则 common 反向依赖某 server 的 `grpc_gen/` 形成循环依赖。附带收益：生成代码用 `add_custom_command` 输出到 build 目录，不入源码树。
- **合并 `constants.h`**：common 必须依赖 `ErrorCodes`（`status_grpc_client.cpp` 用到）。`ErrorCodes` 本质是 **gRPC 协议契约**（client/server 共享的整数约定），属基础设施；`MSG_IDS`、`USER_TOKEN_PREFIX` 也跨 server 共享，一并合并。
- **`UID_INVALID=8 / TOKEN_INVALID=9` 取值**：两套原值冲突，取 ChatServer 顺序。已验证改值不破坏 wire protocol（GateServer 只判断 `error()` 真值；ChatClient 无这两个枚举；无代码把其发给客户端）。
- **回包用 `Defer`**：作用域受限、不像宏污染翻译单元、现代 C++ 风格。
- **不统一 `CMAKE_RUNTIME_OUTPUT_DIRECTORY`**：三个 server 的 `config.ini` 会互相覆盖；`config_manager` 依赖 `./config.ini` 相对路径。改为各 server 落在 `build/<server>/`，`start.sh` 已适配。
- **common 依赖用 `PUBLIC` 传递**：静态库链接依赖默认不传递，用 `PRIVATE` 会导致 server 报 undefined reference。代价是 server 被迫接受 common 选定的 gRPC/Boost 版本——正是想要的"统一约定"。
- **保留 ChatClient 不动**：改造范围是三个 Server。ChatClient 是 Qt 应用，有独立 `constants.h`（含 `RequestId` 等 Qt 专属枚举）与 `Singleton<T>`，强行合并会把 Qt 依赖污染到 server。

## 四、改造范围

| 改了 | 没改 |
|---|---|
| 基础设施集中到 `common/` | 业务代码（cserver/csession/logic_system）原位不动 |
| 单例从模板继承改为 Meyers' | namespace `P1` 不变 |
| proto 统一到 `common/proto/` | 各 server `config.ini` 内容不动 |
| 错误码值统一 | gRPC wire protocol 不变（向后兼容） |
| 顶层 CMakeLists 统一入口 | ChatClient 仍独立 |
| 启动脚本适配新 build 结构 | 日志目录保持 `chat/logs/` |

业务代码改动仅限：`#include "xxx.h"` → `"chat/xxx.h"`；`USERTOKENPREFIX`/`TOKEN_KEY_PREFIX` → `USER_TOKEN_PREFIX`；`auto pool = ...` → `auto& pool = ...`（避免触发已删除的拷贝构造）。业务逻辑、类结构、gRPC 接口均未触碰。

## 五、遗留问题（下一阶段）

| 遗留 | 说明 |
|---|---|
| 单例仍是硬编码依赖 | `RedisManagerPool::GetInstance()` 直接调用，测试中无法替换；需抽象接口 + 依赖注入 |
| 构造函数内做 IO | 单例构造即初始化连接池/配置，无法脱离真实 Redis/MySQL 实例化 |
| ~~错误码未统一~~ | ✅ 已收敛：VerifyServer（Node.js）特有码改用 `20001+` 预留段，与 C++ `ErrorCodes` 同表、不再撞值 |
| 无测试 | 本次重构无任何自动化测试保护，仅手工回归验证 |

## 六、后续演进方向

1. **业务层抽象**：`logic_system` 路由注册模式、`cserver` 基类可进一步抽象（`BaseServer`、`RouteRegistry`）
2. **接口化 + 依赖注入**：`IRedisCache` / `IUserRepository` 抽接口 + Fake 实现，业务逻辑可脱离真实中间件做单测
3. **proto 拆分**：`message.proto` 可拆为 `verify.proto` / `status.proto` / `chat.proto`
4. **ChatClient 接入 common**：Qt 项目 link `chat_common` 复用 `ErrorCodes`、`RedisManager` 等
5. **统一日志框架**：引入 spdlog/glog 替代 nohup 重定向，支持结构化日志与自动轮转
6. **补齐测试与 CI**：优先协议层（帧编解码、负载均衡选点）纯函数单测，再逐步覆盖业务 Handler

## 参考资料

- 改造前代码状态：见 git history（`singleton.h` 模板继承、三份 `grpc_gen/`、三份 `constants.h`）
- CMake Modern Targets：https://cmake.org/cmake/help/latest/manual/cmake-targets.7.html
- Meyers' Singleton：Effective C++ Item 4 / S. Meyers
- gRPC C++ codegen：https://grpc.io/docs/languages/cpp/basics/
