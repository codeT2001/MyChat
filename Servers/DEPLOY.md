# Chat 项目部署指南

本文档描述服务端集群（`Servers/`）在单台 Linux 服务器上的完整部署流程。Qt 客户端的构建见 [../Client/README.md](../Client/README.md)。

## 目录

- [一、部署准备](#一部署准备)
- [二、初始化数据库](#二初始化数据库)
- [三、构建](#三构建)
- [四、快速启动](#四快速启动)
- [五、故障排查](#五故障排查)
- [六、停止服务](#六停止服务)
- [七、文件结构](#七文件结构)
- [八、注意事项](#八注意事项)

---

## 一、部署准备

### 1. 系统依赖

| 依赖 | 版本要求 | 用途 |
|---|---|---|
| Redis | 5.0+ | token / 验证码 / 用户资料缓存 / 路由 / 在线计数 |
| MySQL | 5.7+（建议 8.x，端口 3306） | 用户、好友申请、好友关系持久化 |
| Node.js + npm | 14+ | VerifyServer 邮件验证码服务 |
| CMake | **3.20+** | C++ 服务构建（项目 CMakeLists 显式要求 3.20） |
| g++ | GCC 12+（支持 C++17） | C++ 编译 |
| Ninja | — | 构建后端（推荐，非必须） |
| Boost | 1.74+（asio / beast / uuid / lexical_cast） | HTTP、TCP、I/O 线程池、token |
| gRPC + Protobuf | 含 `grpc_cpp_plugin` | 服务间 RPC |
| mysql-connector-c++ | 8.0（mysqlcppconn） | MySQL 驱动 |
| hiredis + redis++ | sw::redis++ | Redis 客户端 |
| jsoncpp | — | JSON 解析 |
| OpenSSL | — | mysqlcppconn 依赖（传递引入） |

**一键安装（Ubuntu 22.04）**

```bash
sudo apt update
sudo apt install -y build-essential git cmake ninja-build pkg-config libssl-dev zlib1g-dev \
    mysql-server redis-server libmysqlcppconn-dev libhiredis-dev libjsoncpp-dev \
    libboost-all-dev
# gRPC/Protobuf、redis++ 需按官方文档源码安装
```

> ⚠️ Ubuntu 20.04 自带的 CMake 版本可能低于 3.20，需自行升级（官方 apt 源或 Kitware 源）。

### 2. 端口规划

| 服务 | 端口 | 绑定地址 | 是否需对外开放 |
|---|---|---|---|
| GateServer（HTTP） | 9090 | `0.0.0.0`（main.cpp 默认） | **是**，客户端 HTTP 接入 |
| ChatServer1（TCP） | 50061 | 由 StatusServer 对外发布（config 中为公网 IP） | **是**，客户端长连接 |
| ChatServer2（TCP） | 50062 | 同上 | **是** |
| ChatServer1（gRPC） | 50081 | 127.0.0.1 | 否，节点间内部通信 |
| ChatServer2（gRPC） | 50082 | 127.0.0.1 | 否 |
| StatusServer（gRPC） | 50052 | 127.0.0.1 | 否 |
| VerifyServer（gRPC） | 50051 | 127.0.0.1 | 否 |
| Redis | 6379 | 127.0.0.1 | 否 |
| MySQL | 3306 | 127.0.0.1 | 否 |

> 各 gRPC 客户端默认连接 127.0.0.1；如服务分布在多台机器，需同步修改各 `config.ini` 中的 Host 与防火墙策略。

---

## 二、初始化数据库

### 1. 建库与授权

```sql
CREATE DATABASE chat_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
-- 用户名/口令替换为实际值，并与 Servers/.env 中的 MYSQL_USER / MYSQL_PASSWORD 保持一致
CREATE USER 'your_mysql_user'@'localhost' IDENTIFIED BY 'your_mysql_password';
GRANT ALL PRIVILEGES ON chat_db.* TO 'your_mysql_user'@'localhost';
FLUSH PRIVILEGES;
```

### 2. 建表

以下 DDL 依据 [common/src/mysql_dao.cpp](common/src/mysql_dao.cpp) 中实际执行的 SQL 整理（共 4 张表；列长度仅为建议，代码不依赖具体长度）：

```sql
USE chat_db;

-- ① 全局发号器：注册事务中 UPDATE user_id SET id = LAST_INSERT_ID(id+1)
--    ⚠️ 漏建此表，注册会直接失败
CREATE TABLE user_id (
    id INT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
INSERT INTO user_id (id) VALUES (0);

-- ② 用户账号资料
CREATE TABLE `user` (
    uid   INT NOT NULL PRIMARY KEY,
    name  VARCHAR(64)  NOT NULL UNIQUE,
    email VARCHAR(128) NOT NULL UNIQUE,
    pwd   VARCHAR(64)  NOT NULL,
    icon  VARCHAR(255) DEFAULT '',
    nick  VARCHAR(64)  DEFAULT '',
    sex   TINYINT      DEFAULT 0,
    `desc` VARCHAR(255) DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ③ 好友申请（status: 0 待处理 / 1 同意 / 2 拒绝）
CREATE TABLE friend_apply (
    id       BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    from_uid INT NOT NULL,
    to_uid   INT NOT NULL,
    status   TINYINT NOT NULL DEFAULT 0,
    UNIQUE KEY uk_from_to (from_uid, to_uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ④ 好友关系（认证同意后双向插入）
CREATE TABLE friend (
    self_id   INT NOT NULL,
    friend_id INT NOT NULL,
    back      VARCHAR(64) DEFAULT '',
    UNIQUE KEY uk_self_friend (self_id, friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

> 用最小权限账号更安全：把 `GRANT ALL` 换成 `GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.*`。

---

## 三、构建

### 1. C++ 服务端

```bash
cd <repo>/Servers                # ⚠️ 入口是 Servers/，仓库根目录没有 CMakeLists
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)
```

产物布局：

```
build/common/libchat_common.a
build/common/grpc_gen/           # proto 生成物
build/GateServer/gateServer
build/ChatServer/chatServer
build/StatusServer/statusServer
```

> 首次全量构建耗时较长（gRPC + Boost 相关目标编译量大）。调试时可用 `-j1` 单线程编译以便看清报错位置。

### 2. VerifyServer

```bash
cd <repo>/Servers/VerifyServer
npm install
```

确认 [config.json](VerifyServer/config.json) 中 QQ 邮箱账号 / 授权码（SMTP）与 Redis 地址可用；可用 `node test-smtp.js` 手动验证 SMTP 连通性。

> ⚠️ **`config.json` 内含真实邮箱授权码**，请立即替换为你自己的凭据，并确保该文件**不会被提交到代码仓库**（已泄露的凭据应尽早在邮箱后台重置）。

### 3. 客户端（可选，在开发机构建）

```bash
cd <repo>/Client
cmake -S . -B build && cmake --build build -j$(nproc)
```

客户端默认连接 `http://81.69.247.52:9090`（写死在 [../Client/src/utils.cpp](../Client/src/utils.cpp)），部署到其他服务器时需修改 `SERVER_HOST` 后重新编译。

---

## 四、快速启动

所有运维脚本位于 `Servers/` 目录：

```bash
cd <repo>/Servers

./start.sh    # 启动全部服务
./status.sh   # 查看各服务 PID / 端口 / 内存
./stop.sh     # 优雅停止
```

`start.sh` 的启动顺序（每步带进程检查，失败即退出）：

```
Redis → VerifyServer → GateServer → ChatServer1/2 → StatusServer
```

> **为什么 StatusServer 最后启动**：它需要从已在线的 ChatServer 读取 `login_count` 才能正确做最小负载选择。

### 启动后冒烟验证

```bash
# ① 网关健康
curl -i http://127.0.0.1:9090/

# ② 验证码链路（观察日志确认 SMTP 状态）
curl -X POST http://127.0.0.1:9090/getVerifyCode \
     -H 'Content-Type: application/json' \
     -d '{"email":"test@example.com"}'

# ③ 全部端口在听
ss -ltnp | grep -E ':(9090|50051|50052|50061|50062|50081|50082|6379|3306)'
```

### 服务说明

| 服务 | 端口 | 说明 |
|---|---|---|
| Redis | 6379 | 缓存和会话/路由/计数 |
| MySQL | 3306 | 用户与好友关系持久化 |
| VerifyServer | 50051 | Node.js 邮件验证码服务（gRPC） |
| GateServer | 9090 | HTTP 网关：验证码/注册/改密/登录入口 |
| ChatServer1 | TCP 50061 / gRPC 50081 | 聊天节点 1（好友与文本消息） |
| ChatServer2 | TCP 50062 / gRPC 50082 | 聊天节点 2 |
| StatusServer | 50052 | 节点分配（最小连接数）与 token 颁发 |

### 日志查看

`logs/` 目录由 `start.sh` 自动创建，各服务以 nohup 重定向输出：

```bash
tail -f logs/verify_server.log    # VerifyServer
tail -f logs/gate_server.log      # GateServer
tail -f logs/chat_server1.log     # ChatServer1
tail -f logs/chat_server2.log     # ChatServer2
tail -f logs/status_server.log    # StatusServer
```

---

## 五、故障排查

### 1. 检查服务状态

```bash
./status.sh
```

### 2. 检查端口监听

```bash
ss -tlnp | grep -E ':(9090|50051|50052|50061|50062|50081|50082|6379|3306)'
# 或
netstat -tlnp | grep -E ':(9090|50051|50052|50061|50062|50081|50082|6379|3306)'
```

### 3. 手动测试基础组件

```bash
# Redis
redis-cli ping

# MySQL（凭据来自 Servers/.env，避免口令进入 shell 历史）
source .env
mysql -h"${MYSQL_HOST:-127.0.0.1}" -P"${MYSQL_PORT:-3306}" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" chat_db -e "SELECT 1"

# VerifyServer（确认 gRPC 端口在监听）
ss -tlnp | grep ':50051'

# GateServer（HTTP 自测）
curl -i http://127.0.0.1:9090/
```

### 4. 常见问题

| 现象 | 排查方向 |
|---|---|
| `cmake: CMakeLists.txt not found` | 你不在 `Servers/`。仓库根目录**没有** CMakeLists |
| CMake 报版本过低（要求 3.20） | `cmake --version`；Ubuntu 20.04 需升级 CMake |
| 编译找不到 `chat/xxx.h` | 确认在 `Servers/` 下构建后重新 `cmake -S . -B build -G Ninja` |
| 注册失败 / uid 异常 | 检查是否漏建 **`user_id` 发号器表** |
| 登录提示 USER_ALREADY_LOGIN(10) | Redis 中残留 `userver_{uid}`（上次会话未正常清理），确认无人在线后 `redis-cli DEL userver_<uid>` |
| 登录成功但客户端连不上聊天节点 | `StatusServer/config.ini` 的 ChatServer Host 不可达（本地部署需改 `127.0.0.1`） |
| 获取验证码失败 | 查 `verify_server.log`：QQ 邮箱授权码是否过期、SMTP 是否被限流；Redis 中 `code_{email}` 是否已有值 |
| GateServer 启动即退出 | 9090 端口占用，或 [GateServer/config.ini](GateServer/config.ini) 中 MySQL/Redis 配置不可达 |
| ChatServer 停止慢 | 属正常的优雅退出（清 Redis、停 I/O 池、关 gRPC），`stop.sh` 有 60 秒宽限，超时才 SIGKILL |
| 配置加载失败 | 可执行文件与 `config.ini` 必须同目录（`build/<server>/`） |

---

## 六、停止服务

```bash
cd <repo>/Servers
./stop.sh
```

`stop.sh` 的停止顺序与启动相反，**不会停止 Redis 和 MySQL**（基础组件需保持运行）：

```
GateServer → ChatServer1/2 → StatusServer → VerifyServer
```

GateServer 与两个 ChatServer 均先发 SIGTERM 并循环等待最多 60 秒，超时后 `pkill -9` 强制终止；StatusServer、VerifyServer 直接 SIGTERM。如需连 Redis 一起停，手动执行 `redis-cli shutdown`。

---

## 七、文件结构

```
chat/
├── Client/                      # Qt 6 桌面客户端（独立 CMake 项目）
│   ├── CMakeLists.txt
│   ├── src/ inc/                #   实现与头文件
│   ├── ui/ style/ images/       #   Designer 界面、QSS 样式、图片资源
│   └── README.md
│
└── Servers/                     # 服务端集群（运维脚本均在此目录执行）
    ├── CMakeLists.txt           # 顶层 CMake（add_subdirectory: common → 3 个 C++ server）
    ├── start.sh / stop.sh / status.sh
    ├── README.md / DEPLOY.md / REFACTORING.md
    │
    ├── common/                  # 公共静态库 libchat_common.a
    │   ├── CMakeLists.txt
    │   ├── include/chat/        #   公共头文件（#include "chat/xxx.h"）
    │   ├── src/                 #   Redis/MySQL/gRPC 客户端等实现
    │   ├── proto/message.proto  #   全局唯一 proto（生成到 build/common/grpc_gen/）
    │   └── README.md
    │
    ├── GateServer/              # HTTP 网关服务（源码）
    │   ├── main.cpp / config.ini
    │   └── src/                 #   cserver / http_connection / logic_system
    │
    ├── ChatServer/              # TCP 聊天节点（源码，可多实例）
    │   ├── main.cpp
    │   ├── config.ini           #   ChatServer1：TCP 50061 / gRPC 50081
    │   ├── config2.ini          #   ChatServer2：TCP 50062 / gRPC 50082
    │   └── src/                 #   cserver / csession / logic_system / gRPC 客户端与服务端
    │
    ├── StatusServer/            # gRPC 协调服务（源码）
    │   ├── main.cpp / config.ini
    │   └── src/status_service_impl.*
    │
    ├── VerifyServer/            # Node.js 邮件验证码服务
    │   ├── server.js / email.js / redis.js / proto.js
    │   ├── config.json / package.json / message.proto
    │   └── README.md
    │
    ├── build/                   # 统一构建目录（cmake -S . -B build）
    │   ├── common/              #   libchat_common.a 与 proto 生成物
    │   ├── GateServer/          #   gateServer 可执行文件 + config.ini
    │   ├── ChatServer/          #   chatServer + config.ini / config2.ini
    │   └── StatusServer/        #   statusServer + config.ini
    │
    └── logs/                    # 各服务运行日志（nohup 重定向输出）
        ├── verify_server.log    #   VerifyServer
        ├── gate_server.log      #   GateServer
        ├── chat_server1.log     #   ChatServer1
        ├── chat_server2.log     #   ChatServer2
        └── status_server.log    #   StatusServer
```

> 注意：各 server 源码目录下**不再有独立的 build/ 子目录**，编译产物统一输出到 `Servers/build/<server>/`，可执行文件与其 config.ini 同处一目录；`start.sh` 已按该布局 `cd` 到对应目录启动。

---

## 八、注意事项

1. **凭据与配置初始化**：含凭据的真实配置不入库——首次部署将各 `config.ini.example` / `config2.ini.example` / `config.json.example` 复制为去掉 `.example` 后缀的真实文件并直接填入本机明文值（这些文件已被 `.gitignore` 忽略）；`Servers/.env` 仅供 `status.sh` 等脚本使用（`cp .env.example .env`，建议 `chmod 600`），缺少它不影响服务启动。
2. **启动顺序**：Redis → VerifyServer → GateServer → ChatServer1/2 → StatusServer（务必让 StatusServer 最后启动）。
3. **防火墙 / 安全组**：对外开放 **9090** 与 ChatServer TCP 端口 **50061、50062**；50051 / 50052 / 50081 / 50082 / 6379 / 3306 仅本机访问。
4. **客户端连接**：客户端需能访问服务器的公网 IP（或在 StatusServer `config.ini` 的 `[ChatServers]` 中配置可达 Host）。
5. **服务重启后检查**：确认 Redis 中无残留的 `userver_{uid}`、`utoken_{uid}` 键影响再次登录。
6. **凭据安全**：`.env`、真实 `config.ini`、`VerifyServer/config.json` 均已被 `.gitignore` 忽略，不得入库或外发；历史提交中的旧凭据应视为已泄露，需轮换并用 `git filter-repo` 清理历史。
7. **生产加固清单**（当前均未实现）：HTTPS/TLS 终止、GateServer 限流、gRPC 鉴权（mTLS 或服务间 token）、密码 bcrypt 哈希、日志脱敏。

---

## 九、附：使用 systemd 托管（可选）

若希望服务开机自启、崩溃自动拉起，可为每个 C++ 服务写 unit 文件（以 GateServer 为例）：

```ini
# /etc/systemd/system/chat-gate.service
[Unit]
Description=Chat GateServer
After=network.target mysql.service redis-server.service

[Service]
Type=simple
WorkingDirectory=/opt/chat/Servers/build/GateServer
ExecStart=/opt/chat/Servers/build/GateServer/gateServer
Restart=on-failure
RestartSec=3
StandardOutput=append:/opt/chat/Servers/logs/gate_server.log
StandardError=append:/opt/chat/Servers/logs/gate_server.log

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now chat-gate
```

> `WorkingDirectory` 必须是可执行文件与 `config.ini` 同目录，否则配置加载会失败。ChatServer 的两个实例需要各自的 unit（`config.ini` / `config2.ini`）。
