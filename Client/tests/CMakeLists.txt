# ==============================================================================
# Client/tests/CMakeLists.txt —— 客户端单元测试（CTest + Qt Test）
#
# 只编译被测的「纯逻辑」源文件：不建网络连接、不加载 QSS、不实例化界面，
# 因此可在无服务端、无显示器的环境（QT_QPA_PLATFORM 无关）下运行。
# Qt Test 随 Qt 一起提供（Qt6::Test），不引入任何第三方测试框架。
#
# 运行方式：
#   cmake -S . -B build && cmake --build build -j1
#   ctest --test-dir build --output-on-failure
# ==============================================================================

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 每个用例同时落一份文本日志（覆盖写）：部分终端/CI 下 QTest 的 stdout 抓不到，
# 失败时直接看 build/tests/reports/<测试名>.txt 里的期望值与实际值
set(TEST_REPORT_DIR ${CMAKE_CURRENT_BINARY_DIR}/reports)
file(MAKE_DIRECTORY ${TEST_REPORT_DIR})

set(CLIENT_SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(CLIENT_INC_DIR ${CMAKE_SOURCE_DIR}/inc)
set(CLIENT_ROOT_DIR ${CMAKE_SOURCE_DIR})
set(REPO_ROOT_DIR ${CMAKE_SOURCE_DIR}/..)

# 组装一个测试可执行文件并注册到 CTest
function(client_add_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE ${CLIENT_INC_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    # 注入源码树路径：契约测试需要读取 Client/ 与 Servers/ 下的权威定义
    target_compile_definitions(${test_name} PRIVATE
        CHAT_CLIENT_ROOT="${CLIENT_ROOT_DIR}"
        CHAT_REPO_ROOT="${REPO_ROOT_DIR}")
    target_link_libraries(${test_name} PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui) # constants.h 的 MsgInfo 带 QPixmap，编译需要 Gui 头
    add_test(NAME ${test_name}
        COMMAND ${test_name} -o -,txt -o ${TEST_REPORT_DIR}/${test_name}.txt,txt)
endfunction()

# 协议契约：RequestId / ErrorCodes / AcceptAction / HttpPaths 与服务端逐项同值
client_add_test(test_protocol_contract
    ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol_contract.cpp)

# TCP 帧编解码 + 帧长上限（与服务端 msg_codec 契约）
client_add_test(test_msgcodec
    ${CMAKE_CURRENT_SOURCE_DIR}/test_msgcodec.cpp
    ${CLIENT_SRC_DIR}/msgcodec.cpp)

# JSON 编解码：请求序列化 / 回包解析 / 错误字段语义
client_add_test(test_jsoncodec
    ${CMAKE_CURRENT_SOURCE_DIR}/test_jsoncodec.cpp
    ${CLIENT_SRC_DIR}/jsoncodec.cpp
    ${CLIENT_SRC_DIR}/domainmodels.cpp)

# 工具函数：表单校验 / 服务器地址与环境变量 / 头像路径 / 文件大小格式化
# （utils.cpp 含 Widgets 符号引用，链接 Widgets，但测试本身不建界面）
# 带 Q_OBJECT 的头文件必须列进目标源，否则 AUTOMOC 不会为它生成 moc_*.cpp（链接期报 staticMetaObject 未定义）
client_add_test(test_utils
    ${CMAKE_CURRENT_SOURCE_DIR}/test_utils.cpp
    ${CLIENT_INC_DIR}/utils.h
    ${CLIENT_SRC_DIR}/utils.cpp)
target_link_libraries(test_utils PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets)

# 用户数据管理层：申请/好友列表、分页游标、备注与标签、消息追加
client_add_test(test_usermanager
    ${CMAKE_CURRENT_SOURCE_DIR}/test_usermanager.cpp
    ${CLIENT_INC_DIR}/usermanager.h
    ${CLIENT_SRC_DIR}/usermanager.cpp
    ${CLIENT_SRC_DIR}/domainmodels.cpp)

# 资源登记：rc.qrc 与 images/、style/ 目录一一对应（新增资源忘记登记会失败）
client_add_test(test_qrc_assets
    ${CMAKE_CURRENT_SOURCE_DIR}/test_qrc_assets.cpp)

# 界面交互：用 QTest 派发鼠标/键盘事件（等价于手工点界面），offscreen 平台运行。
# 只编译登录框及其依赖链（AuthService 单例在构造期建立信号连接，不会发请求），
# 不触发真实网络：用例只走到「校验/提示/页面切换/错误回显」这些本地分支。
client_add_test(test_ui_widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_widgets.cpp
    ${CLIENT_INC_DIR}/logindialog.h
    ${CLIENT_INC_DIR}/authservice.h
    ${CLIENT_INC_DIR}/httpmanager.h
    ${CLIENT_INC_DIR}/tcpmanager.h
    ${CLIENT_INC_DIR}/usermanager.h
    ${CLIENT_INC_DIR}/utils.h
    ${CLIENT_INC_DIR}/countdownbutton.h
    ${CLIENT_INC_DIR}/statefulclicklabel.h
    ${CLIENT_INC_DIR}/messagetextedit.h
    ${CLIENT_SRC_DIR}/logindialog.cpp
    ${CLIENT_SRC_DIR}/authservice.cpp
    ${CLIENT_SRC_DIR}/httpmanager.cpp
    ${CLIENT_SRC_DIR}/tcpmanager.cpp
    ${CLIENT_SRC_DIR}/msgcodec.cpp
    ${CLIENT_SRC_DIR}/usermanager.cpp
    ${CLIENT_SRC_DIR}/domainmodels.cpp
    ${CLIENT_SRC_DIR}/jsoncodec.cpp
    ${CLIENT_SRC_DIR}/utils.cpp
    ${CLIENT_SRC_DIR}/countdownbutton.cpp
    ${CLIENT_SRC_DIR}/statefulclicklabel.cpp
    ${CLIENT_SRC_DIR}/messagetextedit.cpp
    ${CMAKE_SOURCE_DIR}/ui/logindialog.ui
    ${CMAKE_SOURCE_DIR}/rc.qrc)
target_link_libraries(test_ui_widgets PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)
# 无显示器/无 X11 也能跑：Qt 离屏平台插件
set_tests_properties(test_ui_widgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 主面板交互：侧边栏切页 / 搜索覆盖页 / 点击会话进入聊天页。
# 主面板会拉起几乎全部 UI 组件（会话列表、聊天页、通讯录、资料页……），
# 这里直接复用 Client/src 下的实现（排除 main.cpp / mainwindow.cpp），避免维护第二份依赖清单。
file(GLOB PANEL_TEST_SOURCES CONFIGURE_DEPENDS ${CLIENT_SRC_DIR}/*.cpp)
list(REMOVE_ITEM PANEL_TEST_SOURCES
    ${CLIENT_SRC_DIR}/main.cpp
    ${CLIENT_SRC_DIR}/mainwindow.cpp)
file(GLOB PANEL_TEST_UIS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/ui/*.ui)
file(GLOB PANEL_TEST_HEADERS CONFIGURE_DEPENDS ${CLIENT_INC_DIR}/*.h)
list(REMOVE_ITEM PANEL_TEST_HEADERS ${CLIENT_INC_DIR}/mainwindow.h) # 其 .cpp 未纳入，避免 moc 引用未定义符号

client_add_test(test_mainpanel
    ${CMAKE_CURRENT_SOURCE_DIR}/test_mainpanel.cpp
    ${PANEL_TEST_HEADERS}
    ${PANEL_TEST_SOURCES}
    ${PANEL_TEST_UIS}
    ${CMAKE_SOURCE_DIR}/rc.qrc)
target_link_libraries(test_mainpanel PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)
set_tests_properties(test_mainpanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
