# Repository Guidelines

## 项目结构与模块划分

ChatClient 是基于 Qt 6 / C++17 的即时通讯桌面客户端，采用「界面 → 服务 → 通信 / 数据」三层结构。

- `src/`：全部实现（`.cpp`）
- `inc/`：全部头文件（`.h`）
- `ui/`：Qt Designer 界面文件（`.ui`）
- `style/`：QSS 样式，`style/_design_tokens.qss` 是颜色与间距的规范参考
- `images/`：图标、头像等图片资源
- `rc.qrc`：资源清单，新增样式或图片必须在此登记

分层约束：UI 只调用 Service 层（`AuthService` / `FriendService` / `ChatService`），Service 只调用 Manager 层（`HttpManager` / `TcpManager` / `UserManager`），禁止界面代码直接使用网络单例。

## 构建、测试与开发命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

- Qt 不在 `PATH` 中时追加 `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64`。
- 运行：Windows 用 `build\ChatClient.exe`，Linux / macOS 用 `./build/ChatClient`。
- 运行前须把 `src/utils.cpp` 中的 `SERVER_HOST` / `SERVER_PORT` 改成可达的 GateServer 地址，本地调试用 `127.0.0.1`。
- GUI 无控制台，日志落在可执行文件旁的 `logs/chatclient-<PID>.log`，排查问题以该文件为准。

## 编码风格与命名规范

- 格式统一由 `.clang-format` 约束（LLVM 基线）：4 空格缩进、禁用 Tab、行宽 120、指针与引用靠右。
- 类名用 `PascalCase`（如 `ChatService`），文件名全小写（`chatservice.cpp`），成员变量尾随下划线（如 `handles_`）。
- 信号以 `Sig` 开头（如 `SigTextChatMsgReceived`），槽以 `On` 开头；单例统一提供 `GetInstance()`。
- 头文件用 `#ifndef XXX_H` 宏保护；日志统一走 `LOG_INFO()` / `LOG_WARN()` 等宏，不要直接写 `qDebug`。
- `#include` 的大小写必须与文件名完全一致，否则 Linux 构建会失败（Windows 能编过）。

## 测试指南

仓库目前没有单元测试与 CI，改动通过「构建 + 手工回归」验证：编译成功后运行客户端，依次覆盖登录、注册、找回密码、搜索加好友、收发消息，并确认日志中没有 `LOG_ERROR`。提交前至少保证一次干净的全量构建通过。

## 提交与 Pull Request 规范

- git push 前必须先执行 git pull --rebase，保持线性历史、避免多余的合并提交。
- 提交信息用中文，推荐 `feat:` / `fix:` / `refactor:` 前缀加一句话摘要，正文用 `-` 列表逐条说明改动点（可参考 `git log`）。
- 新增 `.cpp` / `.h` / `.ui` 无需改 `CMakeLists.txt`（`GLOB CONFIGURE_DEPENDS` 自动纳入），但新增资源必须写进 `rc.qrc`。
- PR 需说明改动所属层次（UI / Service / 通信 / 数据）与影响范围，并关联对应 issue；界面改动附截图，协议或消息 ID 变更需说明与服务端的对齐情况。

## 安全与配置提示

- 服务器地址目前硬编码在 `src/utils.cpp`，不要提交真实生产地址或凭据。
- 不要在客户端持久化密码；登录 token 只保存在内存中的 `UserManager`，退出即失效。