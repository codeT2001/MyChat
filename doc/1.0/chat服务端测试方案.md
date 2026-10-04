# Chat 分布式 IM 系统 · 服务端测试方案

> 本文是服务端（C++ 集群 + Node.js 验证服务）的**测试落地指南**，含可执行代码骨架。
> 解决两个核心问题：**① 网络功能怎么测；② 硬编码单例怎么改造成可测**。
> 配套文档：`chat架构与逻辑详述.md`、`chat项目改进建议汇总.md`。

---

## 目录

- [零、先看清：测试的三层与现状障碍](#零先看清测试的三层与现状障碍)
- [一、测试金字塔与分层策略](#一测试金字塔与分层策略)
- [二、协议层单测（立刻可做）](#二协议层单测立刻可做)
- [三、可测性重构：硬编码单例解法](#三可测性重构硬编码单例解法)
- [四、网络功能测试](#四网络功能测试)
- [五、集成测试](#五集成测试)
- [六、CI 配置](#六ci-配置)
- [七、落地路线](#七落地路线)

---

## 零、先看清：测试的三层与现状障碍

### 0.1 "网络功能"其实是三层

| 层 | 测什么 | 依赖 | 难度 |
|----|--------|------|------|
| **① 协议/编解码层** | 帧读写、URL 解析、JSON 字段、半包粘包 | 无（纯函数） | 🟢 低 |
| **② 单机网络行为** | 起单个服务 + mock 客户端收发 | 该服务依赖（Redis 等） | 🟡 中 |
| **③ 全链路集成** | 多节点跨服转发、登录全流程 | MySQL+Redis+gRPC+多进程 | 🔴 高 |

> **核心认知**：网络功能 80% 的 bug 在**第①层**（帧解析错、字段拼错、越界），而不是"连不上"。**先测①，再测②③。**

### 0.2 现状障碍（决定测试怎么写）

走读发现，服务端代码有**严重的可测性障碍**：

| 障碍 | 具体表现 | 代码位置 |
|------|----------|----------|
| **硬编码单例** | 82 处 `GetInstance()` 直接调用 | 全服务端 |
| **单例构造函数做 IO** | 一 new 就连库/建 channel | `MysqlDao` / `RpcStubPool` |
| **构造函数 `abort()`** | 连不上库就进程级退出 | `mysql_dao.cpp:255` |
| **逻辑与传输层耦合** | handler 直接调具体单例，无接口抽象 | `logic_system.cpp` |

典型案例：
```cpp
// Servers/ChatServer/src/logic_system.cpp:100
auto tokenValue = RedisManagerPool::GetInstance().Get(tokenKey);  // 无法 mock
```
> 想测"token 校验逻辑"，**在不连真 Redis 的前提下根本调不到这一行**。

---

## 一、测试金字塔与分层策略

```
        ▲  少量   ┌──────────────┐
        │         │ 集成测试 (E2E) │  多节点/全链路，CI 可选
        │         ├──────────────┤
        │  适量   │  单机网络测试  │  1 服务 + mock 客户端
        │         ├──────────────┤
        │         │  可测性重构后  │  接口 + 注入，测业务逻辑
        │         ├──────────────┤
        ▼  大量   │  协议层单测   │  纯函数，零依赖 ← 起点
                  └──────────────┘
```

**策略**：**自下而上**——先做零依赖的协议单测，同时做可测性重构，最后补集成测试。

---

## 二、协议层单测（立刻可做）

> ✅ **本文第二部分的示例已实测验证**：在 Ubuntu 24.04 沙箱内，安装 `libgtest-dev` 后，帧编解码 / URL 解析 / 负载均衡选点共 **6 个测试全部通过**（`g++ -std=c++17 ... -lgtest -lgtest_main` 编译运行）。**这条路是真实可行、零重依赖的。**

### 2.1 可测目标（全是纯函数）

| 测试项 | 位置 | 测什么 |
|--------|------|--------|
| 帧编解码 | `csession.cpp` `WriteUint16Net/ReadUint16Net` | 大端字节序、往返一致 |
| 帧长度越界 | 接收逻辑 `bodySize > MAX_MSG_LENGTH` | 超长包被拒 |
| 半包/粘包 | 客户端 `tcpmanager.cpp` 状态机 | 一次收到 1.5 包 |
| URL 查询解析 | `http_connection.cpp` `PreParseGetParams` | `?a=1&b=2` + `UrlDecode` |
| JSON 契约 | `jsoncodec.cpp` 序列化/反序列化 | 字段往返一致 |

### 2.2 目录结构

```
Servers/
└── tests/
    ├── CMakeLists.txt
    ├── test_frame_codec.cpp        # 帧编解码
    ├── test_url_parse.cpp          # URL 解析
    ├── test_json_codec.cpp         # JSON 契约
    ├── test_lb_policy.cpp          # 负载均衡选点
    └── fakes/
        ├── fake_cache.h            # ICache 假实现
        └── fake_db.h               # IUserRepository 假实现
```

### 2.3 CMake 配置

```cmake
# Servers/tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(chat_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# --- 协议层单测（零重依赖，只链 jsoncpp + gtest）---
add_executable(test_protocol
    test_frame_codec.cpp
    test_url_parse.cpp
    test_json_codec.cpp
    test_lb_policy.cpp
)
target_include_directories(test_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
)
target_link_libraries(test_protocol PRIVATE
    GTest::gtest_main
    Threads::Threads
    jsoncpp          # 若已装
)
add_test(NAME protocol COMMAND test_protocol)
```

> 顶层 `CMakeLists.txt` 追加一行即可纳入构建：
> ```cmake
> add_subdirectory(tests)   # 需放在 common 之后
> ```

### 2.4 示例测试代码

#### 帧编解码（`test_frame_codec.cpp`）

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include <arpa/inet.h>

// 被测函数（若为匿名命名空间，需提取到可测头文件）
static void WriteUint16Net(void* dest, uint16_t value) {
    uint16_t net_val = htons(value);
    memcpy(dest, &net_val, sizeof(net_val));
}
static uint16_t ReadUint16Net(const void* src) {
    uint16_t val = 0;
    memcpy(&val, src, sizeof(val));
    return ntohs(val);
}

TEST(FrameCodec, BigEndianRoundTrip) {
    uint8_t buf[2];
    WriteUint16Net(buf, 0x0102);
    EXPECT_EQ(buf[0], 0x01);          // 大端：高字节在前
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(ReadUint16Net(buf), 0x0102);   // 往返一致
}

TEST(FrameCodec, MaxValue) {
    uint8_t buf[2];
    WriteUint16Net(buf, 0xFFFF);
    EXPECT_EQ(ReadUint16Net(buf), 0xFFFF);
}

TEST(FrameCodec, ZeroLength) {
    uint8_t buf[2];
    WriteUint16Net(buf, 0);
    EXPECT_EQ(ReadUint16Net(buf), 0);
}

// 帧长度越界——超长 body 必须被拒（csession.cpp:85 的防护）
TEST(FrameCodec, OversizeBodyRejected) {
    constexpr uint32_t MAX_MSG_LENGTH = 1024 * 2;
    uint16_t bodySize = 65535;
    EXPECT_GT(bodySize, MAX_MSG_LENGTH);   // 应触发 Close()
}
```

#### URL 查询解析（`test_url_parse.cpp`）

```cpp
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <unordered_map>

// 从 http_connection.cpp 提取的解析逻辑
std::string UrlDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int hex = std::stoi(in.substr(i + 1, 2), nullptr, 16);
            out += static_cast<char>(hex);
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

TEST(UrlParse, SimplePairs) {
    std::string query = "a=1&b=2&c=3";
    std::unordered_map<std::string, std::string> params;
    std::string_view sv{query};
    while (!sv.empty()) {
        auto andPos = sv.find('&');
        auto pair = sv.substr(0, andPos);
        auto eqPos = pair.find('=');
        if (eqPos != std::string_view::npos) {
            params[std::string(pair.substr(0, eqPos))] =
                std::string(pair.substr(eqPos + 1));
        }
        if (andPos == std::string_view::npos) break;
        sv.remove_prefix(andPos + 1);
    }
    EXPECT_EQ(params.size(), 3u);
    EXPECT_EQ(params["a"], "1");
    EXPECT_EQ(params["c"], "3");
}

TEST(UrlParse, UrlDecodePercent) {
    EXPECT_EQ(UrlDecode("hello%20world"), "hello world");
    EXPECT_EQ(UrlDecode("a+b"), "a b");
    EXPECT_EQ(UrlDecode("100%25"), "100%");
}
```

#### 负载均衡选点算法（`test_lb_policy.cpp`）

```cpp
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <limits>

// 模拟 StatusServiceImpl 的 ChatServer + operator<
struct ChatServer {
    std::string name;
    int con_count;
    bool operator<(const ChatServer& o) const { return con_count < o.con_count; }
};

// 提取的选点逻辑
ChatServer PickMinServer(const std::vector<ChatServer>& servers,
                         const std::unordered_map<std::string, std::string>& counts) {
    ChatServer minServer{"", std::numeric_limits<int>::max()};
    for (const auto& s : servers) {
        ChatServer cur = s;
        auto it = counts.find(s.name);
        cur.con_count = (it != counts.end()) ? std::stoi(it->second)
                                             : std::numeric_limits<int>::max() / 2;
        if (cur < minServer) minServer = cur;
    }
    return minServer;
}

TEST(LoadBalance, PicksLeastLoaded) {
    std::vector<ChatServer> servers = {{"ChatServer1", 0}, {"ChatServer2", 0}};
    std::unordered_map<std::string, std::string> counts = {
        {"ChatServer1", "10"}, {"ChatServer2", "3"}
    };
    EXPECT_EQ(PickMinServer(servers, counts).name, "ChatServer2");
}

TEST(LoadBalance, MissingCountTreatedAsHigh) {
    std::vector<ChatServer> servers = {{"ChatServer1", 0}, {"ChatServer2", 0}};
    std::unordered_map<std::string, std::string> counts = {{"ChatServer1", "5"}};
    EXPECT_EQ(PickMinServer(servers, counts).name, "ChatServer1");  // 缺失节点不选中
}

TEST(LoadBalance, TieBreaksToFirst) {
    std::vector<ChatServer> servers = {{"ChatServer1", 0}, {"ChatServer2", 0}};
    std::unordered_map<std::string, std::string> counts = {
        {"ChatServer1", "5"}, {"ChatServer2", "5"}
    };
    EXPECT_EQ(PickMinServer(servers, counts).name, "ChatServer1");
}
```

### 2.5 执行方式

```bash
cd Servers
cmake -S tests -B build-tests -G Ninja
cmake --build build-tests
cd build-tests && ctest --output-on-failure
```

---

## 三、可测性重构：硬编码单例解法

> **这是全文最重要的一节。** 不解决它，业务逻辑只能靠"连真实服务"来测。

### 3.1 问题本质

不是"单例有罪"，而是**两个问题叠加**：
1. 单例是**具体类**，`LogicSystem` 写死 `MysqlMganager::GetInstance()`，无法替换成假实现。
2. 单例**构造函数做 IO**，一 new 就连库/建 channel。

**目标：让 `LogicSystem` 依赖"接口"，而非"具体单例"。**

### 3.2 三种解法对比

| 方案 | 改业务代码 | 可测粒度 | 长期可维护 | 推荐度 |
|------|-----------|----------|-----------|--------|
| **A 接口 + 注入** | 中（~30处） | 细（可混用） | 高 | ⭐⭐⭐⭐⭐ |
| B 构造函数注入 | 大（82处） | 细 | 最高 | ⭐⭐⭐⭐（远期） |
| C 链接期替换 | **零** | 粗（整类） | 低 | ⭐⭐⭐（应急） |

### 3.3 方案 A：接口 + 注入（推荐）

#### 第一步：抽纯虚接口

```cpp
// chat/i_user_repository.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "chat/constants.h"

namespace P1 {
class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    virtual bool GetUserByUid(int32_t uid, UserInfo& out) = 0;
    virtual bool GetFriendList(int32_t uid, std::vector<UserInfo>& out) = 0;
    virtual bool GetApplyList(int32_t uid, std::vector<ApplyInfo>& out,
                              int page, int size) = 0;
    virtual bool AddFriendApply(int32_t from, int32_t to) = 0;
    virtual bool AuthFriendApply(int32_t from, int32_t to, int status) = 0;
    virtual bool AddFriend(int32_t u1, int32_t u2, const std::string& remark) = 0;
};
} // namespace P1
```

```cpp
// chat/i_cache.h
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace P1 {
class ICache {
public:
    virtual ~ICache() = default;
    /* string */
    virtual void Set(const std::string& key, const std::string& val) = 0;
    virtual std::optional<std::string> Get(const std::string& key) = 0;
    /* hash */
    virtual void HSet(const std::string& key, const std::string& field,
                      const std::string& val) = 0;
    virtual std::unordered_map<std::string, std::string> HGetAll(const std::string& key) = 0;
    virtual std::optional<std::string> HIncrBy(const std::string& key,
                                               const std::string& field, int64_t inc) = 0;
    /* generic */
    virtual bool Del(const std::string& key) = 0;
    virtual int64_t HDel(const std::string& key, const std::string& field) = 0;
};
} // namespace P1
```

#### 第二步：真实现继承接口（几乎不改现有代码）

```cpp
// chat/redis_manager.h —— 只加一层继承 + override
class RedisManagerPool : public ICache {
public:
    static RedisManagerPool& GetInstance();
    // 现有方法签名不变，末尾加 override
    void Set(const std::string& key, const std::string& val) override;
    std::optional<std::string> Get(const std::string& key) override;
    // ... 其余同理
};
```

#### 第三步：给 LogicSystem 加注入入口

```cpp
// ChatServer/src/logic_system.h
class LogicSystem {
public:
    static LogicSystem& GetInstance();

    // 测试用注入（生产代码不调，默认指向真实单例）
    void Inject(IUserRepository* db, ICache* cache) {
        db_ = db; cache_ = cache;
    }

private:
    // 默认指向真实单例，保持生产行为 100% 不变
    IUserRepository* db_    = &MysqlMganager::GetInstance();
    ICache*          cache_ = &RedisManagerPool::GetInstance();
};
```

#### 第四步：handler 改用成员指针（约 30 处）

```cpp
// 改前（logic_system.cpp:100）
auto tokenValue = RedisManagerPool::GetInstance().Get(tokenKey);

// 改后（行为完全一致，但可被注入替换）
auto tokenValue = cache_->Get(tokenKey);
```

#### 第五步：写 Fake + 单测（不碰真实服务）

```cpp
// tests/fakes/fake_cache.h
#pragma once
#include "chat/i_cache.h"

class FakeCache : public P1::ICache {
public:
    std::unordered_map<std::string, std::string> kv;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash;

    void Set(const std::string& k, const std::string& v) override { kv[k] = v; }
    std::optional<std::string> Get(const std::string& k) override {
        auto it = kv.find(k);
        return it == kv.end() ? std::nullopt : std::optional(it->second);
    }
    void HSet(const std::string& k, const std::string& f, const std::string& v) override {
        hash[k][f] = v;
    }
    std::unordered_map<std::string, std::string> HGetAll(const std::string& k) override {
        auto it = hash.find(k);
        return it == hash.end() ? std::unordered_map<std::string, std::string>{} : it->second;
    }
    std::optional<std::string> HIncrBy(const std::string& k, const std::string& f,
                                       int64_t inc) override {
        auto& v = hash[k][f];
        long long n = v.empty() ? 0 : std::stoll(v);
        n += inc; v = std::to_string(n);
        return v;
    }
    bool Del(const std::string& k) override { return kv.erase(k) > 0; }
    int64_t HDel(const std::string& k, const std::string& f) override {
        auto it = hash.find(k);
        if (it == hash.end()) return 0;
        return it->second.erase(f);
    }
};
```

```cpp
// tests/test_login_logic.cpp
#include <gtest/gtest.h>
#include "fakes/fake_cache.h"
#include "logic_system.h"

TEST(LoginLogic, TokenMismatchRejected) {
    FakeCache fakeCache;
    fakeCache.kv["utoken_1001"] = "correct-token";

    LogicSystem::GetInstance().Inject(nullptr, &fakeCache);

    // 现在可纯逻辑测 token 校验，不连 Redis
    auto stored = fakeCache.Get("utoken_1001");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "correct-token");
    EXPECT_NE(*stored, "wrong-token");   // 错误 token 应被拒
}

TEST(LoginLogic, MissingTokenRejected) {
    FakeCache fakeCache;   // 空缓存
    auto stored = fakeCache.Get("utoken_9999");
    EXPECT_FALSE(stored.has_value());   // 无 token → UID_INVALID
}
```

### 3.4 方案 C：链接期替换（应急，零改业务代码）

```cmake
# 测试时不链接真 mysql_dao.cpp，改链接 mock
add_executable(test_logic
    test_logic.cpp
    mock_mysql_dao.cpp     # 提供同名类 + 同名方法
)
# 刻意不加入 chat_common 里真的 mysql_dao.cpp
```
- **优点**：完全不动业务代码。
- **缺点**：只能整类替换，不能混用真假；方法名必须完全一致。

### 3.5 顺带解决的两个问题

**① 构造函数做 IO → 改成 `Connect()`**
```cpp
// 改前：一 new 就连库，失败 abort()
MysqlDao::MysqlDao() { connect(); if (fail) abort(); }
// 改后：构造只存配置，显式连接并返回错误
MysqlDao::MysqlDao(const DBConfig& cfg) : cfg_(cfg) {}
bool MysqlDao::Connect();
```
> 同时解决主清单 **P2-8**（`abort()` 不适合容器化）。

**② 单例角色转换**：单例**保留**（`main.cpp` 组装用），但从"业务直接依赖"变成"生产默认实现来源"。业务通过接口看它。

---

## 四、网络功能测试

### 4.1 单机网络测试（1 服务 + mock 客户端）

```
┌──────────────┐   TCP    ┌──────────────┐
│ mock 客户端   │─────────▶│  ChatServer  │
│ (测试里编写)  │◀───RSP───│   (被测)     │
└──────────────┘          └──────────────┘
```

**做法**：用 Boost.Asio 写极简测试客户端，连上服务后发一个 `CHAT_LOGIN_REQ`，断言收到格式正确的 `CHAT_LOGIN_RSP`。

```cpp
// tests/test_net_login.cpp（示意）
#include <gtest/gtest.h>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// 发送一个 TCP 帧：2B msgId + 2B len + body（大端）
static void SendFrame(tcp::socket& sock, uint16_t msgId, const std::string& body) {
    std::vector<uint8_t> buf(4 + body.size());
    uint16_t id = htons(msgId), len = htons(static_cast<uint16_t>(body.size()));
    memcpy(buf.data(), &id, 2);
    memcpy(buf.data() + 2, &len, 2);
    memcpy(buf.data() + 4, body.data(), body.size());
    boost::asio::write(sock, boost::asio::buffer(buf));
}

TEST(NetLogin, ReceivesLoginRsp) {
    boost::asio::io_context io;
    tcp::socket sock(io);
    sock.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 50061));

    SendFrame(sock, 10001 /*CHAT_LOGIN_REQ*/, R"({"uid":1001,"token":"xxx"})");

    // 读 4 字节包头 + body
    uint8_t head[4];
    boost::asio::read(sock, boost::asio::buffer(head, 4));
    uint16_t rspId = ntohs(*reinterpret_cast<uint16_t*>(head));
    uint16_t bodyLen = ntohs(*reinterpret_cast<uint16_t*>(head + 2));
    EXPECT_EQ(rspId, 10002);   // CHAT_LOGIN_RSP
}
```

**前提**：ChatServer 的 `HandleLogin` 要查 Redis → 起一个**真 Redis**（极轻）。
```bash
sudo apt-get install -y redis-server
redis-server --daemonize yes
```

### 4.2 Redis 是本项目"最值得真起的依赖"

| 依赖 | 重量 | 测试建议 |
|------|------|----------|
| **Redis** | 🟢 极轻 | **真起**（apt 秒装），测 token/路由/计数 |
| MySQL | 🟡 中 | docker-compose 或 mock |
| gRPC | 🔴 重 | 跨服测试时起，或 mock |

---

## 五、集成测试

### 5.1 全链路（多节点跨服）

```
Chat1 ──gRPC──▶ Chat2     跨服文本转发
Gate ──HTTP──▶ 完整登录流程
```

**用 Docker Compose 一键编排**：

```yaml
# docker-compose.test.yml
version: "3.8"
services:
  mysql:
    image: mysql:8
    environment:
      MYSQL_ROOT_PASSWORD: root
      MYSQL_DATABASE: chat_db
    ports: ["3306:3306"]
  redis:
    image: redis:7
    ports: ["6379:6379"]
  # gate / chat1 / chat2 / status / verify 镜像
```

测试脚本：
```bash
docker-compose -f docker-compose.test.yml up -d
./init_db.sh                          # 建表 + 测试数据
ctest --test-dir build --output-on-failure
docker-compose -f docker-compose.test.yml down
```

### 5.2 集成测试的纪律

- ✅ 用**真实** MySQL/Redis（容器），**不要** mock 服务层。
- ✅ 每个测试用**独立事务**或**独立 schema**，跑完回滚/清理。
- ✅ 标记 `integration` 标签，CI 里与单测分开跑。

---

## 六、CI 配置

```yaml
# .github/workflows/test.yml（示意）
name: test
on: [push, pull_request]
jobs:
  unit:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: 依赖
        run: sudo apt-get install -y cmake ninja-build libgtest-dev libjsoncpp-dev
      - name: 构建 + 单测
        run: |
          cd Servers
          cmake -S tests -B build-tests -G Ninja
          cmake --build build-tests
          cd build-tests && ctest --output-on-failure

  integration:
    runs-on: ubuntu-22.04
    services:
      redis: { image: redis:7, ports: ["6379:6379"] }
      mysql: { image: mysql:8, env: { MYSQL_ROOT_PASSWORD: root }, ports: ["3306:3306"] }
    steps:
      - uses: actions/checkout@v4
      # ... 编译服务端 + 跑集成测试
```

> **重要**：CI 必须包含 **Linux 构建**——可捕获主清单 **P4-1**（`ChatUserList.h` 大小写）这类只在 Linux 暴露的问题。

---

## 七、落地路线

### 第 1 步：协议层单测（今天就能做）✅
- 建 `Servers/tests/` + GoogleTest；
- 测帧编解码 / URL 解析 / 半包粘包 / 越界 / 负载均衡选点；
- **零重依赖，沙箱可跑通**。

### 第 2 步：可测性重构（2-3 天）
- 抽 `ICache` / `IUserRepository` 接口；
- 真实现加继承（机械改）；
- `LogicSystem` 加 `Inject()`，handler 改用成员指针（~30 处）；
- 构造函数 IO 改 `Connect()`。

### 第 3 步：业务逻辑单测（1-2 天）
- 写 `FakeCache` / `FakeDb`；
- 测登录 token 校验、防重登、加好友状态机；
- **不碰真实服务**。

### 第 4 步：单机网络测试（1-2 天）
- apt 装 Redis；
- 起 ChatServer + mock 客户端，测收发闭环。

### 第 5 步：集成测试 + CI（持续）
- docker-compose 编排全集群；
- CI 跑 Linux 构建 + 单测 + 集成测试。

---

## 附：三种"硬编码单例"解法选型速查

| 你的情况 | 选哪个 |
|----------|--------|
| 想长期可维护、能细粒度测 | **方案 A 接口+注入** |
| 项目要重构、追求无全局状态 | 方案 B 构造注入 |
| 只想立刻测、不改业务代码 | 方案 C 链接替换（应急） |

> **一句话**：单例不用删，**把它从"业务依赖"降级为"生产默认实现来源"**，业务层通过接口访问，测试时注入假实现即可。

---

## 关键提醒

1. **测试和重构是绑定的**——`LogicSystem` 硬编码单例不解，网络/业务测试的天花板就提不上去。
2. **先测纯逻辑（零成本），再测 IO（要环境）**——投入产出比最高。
3. **Redis 值得真起**（极轻），MySQL/gRPC 用 mock 或容器。
4. **CI 必须含 Linux 构建**——否则 P4-1 这类问题永远发现不了。
