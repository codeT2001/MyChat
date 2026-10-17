# Chat 项目部署指南

服务端集群（`Servers/`）在单台 Linux 服务器上的完整部署流程。架构与接口见 [README.md](README.md)，Qt 客户端构建见 [../Client/README.md](../Client/README.md)。

## 一、部署准备

### 1. 系统依赖

| 依赖 | 版本要求 | 用途 |
|---|---|---|
| Redis | 5.0+ | token / 验证码 / 用户资料缓存 / 路由 / 在线计数 / 节点注册 / Stream 事件总线 |
| MySQL | 5.7+（建议 8.x，端口 3306） | 用户、好友申请、好友关系持久化 |
| Node.js + npm | 14+ | VerifyServer 邮件验证码服务 |
| CMake | **3.20+** | C++ 服务构建 |
| g++ | GCC 12+（支持 C++17） | C++ 编译 |
| Boost | 1.74+（asio / beast / uuid / lexical_cast） | HTTP、TCP、I/O 线程池、token |
| gRPC + Protobuf | 含 `grpc_cpp_plugin` | 服务间 RPC |
| mysql-connector-c++ | 8.0（mysqlcppconn） | MySQL 驱动 |
| hiredis + redis++ | sw::redis++ | Redis 客户端 |
| jsoncpp | — | JSON 解析 |

```bash
# 一键安装（Ubuntu 22.04）
sudo apt update
sudo apt install -y build-essential git cmake ninja-build pkg-config libssl-dev zlib1g-dev \
    mysql-server redis-server libmysqlcppconn-dev libhiredis-dev libjsoncpp-dev \
    libboost-all-dev
# gRPC/Protobuf、redis++ 需按官方文档源码安装
```

> ⚠️ Ubuntu 20.04 自带的 CMake 可能低于 3.20，需自行升级（官方 apt 源或 Kitware 源）。

### 2. 端口规划

| 服务 | 端口 | 绑定地址 | 需对外开放 |
|---|---|---|---|
| GateServer（HTTP） | 9090 | `0.0.0.0` | **是**，客户端 HTTP 接入 |
| ChatServer1（TCP） | 50061 | 由 StatusServer 对外发布 | **是**，客户端长连接 |
| ChatServer2（TCP） | 50062 | 同上 | **是** |
| ChatServer1（gRPC） | 50081 | 127.0.0.1 | 否，节点间内部通信 |
| ChatServer2（gRPC） | 50082 | 127.0.0.1 | 否 |
| StatusServer（gRPC） | 50052 | 127.0.0.1 | 否 |
| VerifyServer（gRPC） | 50051 | 127.0.0.1 | 否 |
| Redis | 6379 | 127.0.0.1 | 否 |
| MySQL | 3306 | 127.0.0.1 | 否 |

> 多机部署时，需同步修改各 `config.ini` 中的 Host 与防火墙策略。

## 二、初始化数据库

### 1. 建库与授权

```sql
CREATE DATABASE chat_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
-- 用户名/口令替换为实际值，并与 Servers/.env 中的 MYSQL_USER / MYSQL_PASSWORD 保持一致
CREATE USER 'your_mysql_user'@'localhost' IDENTIFIED BY 'your_mysql_password';
GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.* TO 'your_mysql_user'@'localhost';
FLUSH PRIVILEGES;
```

### 2. 建表

依据 [common/src/mysql_dao.cpp](common/src/mysql_dao.cpp) 实际执行的 SQL 整理（共 4 张表；列长度仅为建议，代码不依赖具体长度）：

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
    pwd   VARCHAR(64)  NOT NULL,  -- 存 SHA-256 摘要（64 位十六进制），非明文
    icon  VARCHAR(255) DEFAULT '',
    nick  VARCHAR(64)  DEFAULT '',
    sex   TINYINT      DEFAULT 0,
    `desc` VARCHAR(255) DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
--    ⚠️ pwd 列自本版本起存 SHA-256 摘要：升级前已存在的明文密码行不会被自动迁移，
--       需用户重新注册/改密（或按新算法回填，见 common/include/chat/password_crypto.h）。

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

## 三、构建

### 1. C++ 服务端

```bash
cd <repo>/Servers                # ⚠️ 入口是 Servers/，仓库根目录没有 CMakeLists
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)   # 单目标：--target chatServer
```

### 2. VerifyServer（Node.js）

```bash
cd <repo>/Servers/VerifyServer
npm install
```

确认 [config.json](VerifyServer/config.json) 中 QQ 邮箱账号/授权码（SMTP）与 Redis 地址可用；`node test-smtp.js` 可手动验证 SMTP 连通性。

> ⚠️ `config.json` 内含真实邮箱授权码，请替换为自己的凭据且**不要提交到仓库**；已泄露的凭据应尽早在邮箱后台重置。

### 3. 客户端（可选，在开发机构建）

```bash
cd <repo>/Client
cmake -S . -B build && cmake --build build -j$(nproc)
```

客户端默认连接 `http://127.0.0.1:9090`（[../Client/src/utils.cpp](../Client/src/utils.cpp) 的 `DEFAULT_SERVER_HOST`），
换服务器用环境变量 `CHAT_SERVER_HOST` / `CHAT_SERVER_PORT` 覆盖，无需重编译。

## 四、启动与冒烟

```bash
cd <repo>/Servers
./start.sh    # 启动全部服务
./status.sh   # 查看各服务 PID / 端口 / 内存
./stop.sh     # 优雅停止
```

启动顺序：**Redis → VerifyServer → GateServer → ChatServer1/2 → StatusServer**（每步带进程检查，失败即退出）。

> **为什么 StatusServer 最后启动**：它需要从已在线的 ChatServer 读取 `login_count` 才能正确做最小负载选择。

### 冒烟验证

```bash
# ① 网关健康
curl -i http://127.0.0.1:9090/

# ② 验证码链路（观察日志确认 SMTP 状态）
curl -X POST http://127.0.0.1:9090/getVerifyCode \
     -H 'Content-Type: application/json' -d '{"email":"test@example.com"}'

# ③ 全部端口在听
ss -ltnp | grep -E ':(9090|50051|50052|50061|50062|50081|50082|6379|3306)'

# ④ 节点注册与事件流（ChatServer 启动后）
redis-cli SMEMBERS chat:nodes
redis-cli GET chat:node:ChatServer1
redis-cli XINFO GROUPS chat:push:ChatServer1
```

> `XINFO GROUPS chat:push:ChatServer1` 应看到 group 名 `ChatServer1`。`lag` 长期不下降说明消费端异常，`pending` 持续增长可结合 `XPENDING` 排查。

### 日志

`logs/` 目录由 `start.sh` 自动创建，各服务以 nohup 重定向输出：`verify_server.log` / `gate_server.log` / `chat_server1.log` / `chat_server2.log` / `status_server.log`。

## 五、故障排查

| 现象 | 排查方向 |
|---|---|
| 登录提示 USER_ALREADY_LOGIN(10) | 现为**顶号模式**，旧会话会被自动踢下线；若仍出现说明服务端为旧版本，需重新构建部署 |
| 获取验证码失败 | 查 `verify_server.log`：QQ 邮箱授权码是否过期、SMTP 是否被限流；Redis `code_{email}` 是否已有值 |
| GateServer 启动即退出 | 9090 端口占用，或 [GateServer/config.ini](GateServer/config.ini) 中 MySQL/Redis 配置不可达 |
| ChatServer 停止慢 | 正常的优雅退出（清 Redis、停 I/O 池、关 gRPC），`stop.sh` 有 60 秒宽限，超时才 SIGKILL |
| 跨服通知延迟或丢失 | 查 `redis-cli XINFO GROUPS chat:push:{节点名}` 的 `lag/pending`；确认 `NotifyTransport=stream` 且目标节点 consumer 已启动；排查时可临时切到 `grpc` 直连 |
| 事件持续重试 | `redis-cli XPENDING chat:push:{节点名}` 查看 idle/delivery；检查 `chat:dead:{节点名}` 与目标节点日志（payload 非法、处理异常） |
| 配置加载失败 | 可执行文件与 `config.ini` 必须同目录（`build/<server>/`） |

> 基础组件自测：`redis-cli ping`；`source .env && mysql -h"${MYSQL_HOST:-127.0.0.1}" -P"${MYSQL_PORT:-3306}" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" chat_db -e "SELECT 1"`。通用问题（构建、端口、`user_id` 表等）见 [README.md](README.md#故障排除)。

## 六、停止服务

```bash
cd <repo>/Servers && ./stop.sh
```

停止顺序与启动相反，**不会停止 Redis 和 MySQL**：`GateServer → ChatServer1/2 → StatusServer → VerifyServer`。GateServer 与两个 ChatServer 先发 SIGTERM 并最多等 60 秒，超时后 `pkill -9`；StatusServer、VerifyServer 直接 SIGTERM。需连 Redis 一起停可手动 `redis-cli shutdown`。

## 七、注意事项

1. **凭据与配置**：真实配置不入库——首次部署将各 `config.ini.example` / `config2.ini.example` / `config.json.example` 复制为去掉 `.example` 的文件并填入本机明文值；`Servers/.env`（`cp .env.example .env`）仅供脚本使用，缺失不影响服务启动。
2. **启动顺序**：务必让 StatusServer 最后启动。
3. **防火墙**：对外开放 **9090**、**50061**、**50062**；其余端口仅本机访问。
4. **客户端连接**：需能访问服务器公网 IP；在 ChatServer 的 `[SelfServer] PublicHost` 填写客户端可达地址。StatusServer 直接从节点注册表读取该地址，不再需要静态 `[ChatServerN]` 覆盖。
5. **重启后检查**：确认 Redis 中无残留 `userver_{uid}` / `utoken_{uid}` 键影响再次登录；`chat:nodes` 中不应残留已下线节点（异常退出最长 30 秒由 TTL 清理）。
6. **Stream 运维**：跨服事件默认保留 1 天、每条流最多约 10 万条；节点停机超过 `StreamKeyTtlSeconds` 或事件超过 `StreamMaxEventAgeSeconds` 会被丢弃。普通运维只观察 `lag/pending`，不要随意删除 `chat:push:*`（会丢失未消费事件）。
7. **凭据安全**：`.env`、真实 `config.ini`、`VerifyServer/config.json` 均已被 `.gitignore` 忽略；历史提交中的旧凭据视为已泄露，需轮换并用 `git filter-repo` 清理。
8. **生产加固清单**（当前均未实现）：HTTPS/TLS 终止、GateServer 限流、gRPC 鉴权（mTLS 或服务间 token）、密码 bcrypt 哈希、日志脱敏。

## 八、使用 systemd 托管（可选）

以 GateServer 为例：

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

> `WorkingDirectory` 必须是可执行文件与 `config.ini` 同目录。ChatServer 两个实例需各自的 unit（`config.ini` / `config2.ini`）。
