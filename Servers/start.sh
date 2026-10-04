#!/bin/bash

# Chat项目启动脚本
# 部署顺序: Redis -> VerifyServer -> GateServer -> ChatServer -> StatusServer

# 动态获取脚本所在目录（Servers/），从任意路径调用都能正确定位
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$PROJECT_DIR/logs"

# 创建日志目录
mkdir -p $LOG_DIR

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 可选加载脚本用环境变量文件（.env 不入库，模板见 .env.example）。
# 服务自身从各自本地 config.ini / config.json 读取配置（不依赖环境变量），
# 因此缺少 .env 不影响启动，仅提示（status.sh 的 MySQL 探活会跳过）。
ENV_FILE="$PROJECT_DIR/.env"
if [ -f "$ENV_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    set +a
else
    echo -e "${YELLOW}提示: 未找到 $ENV_FILE，服务可正常启动；status.sh 的 MySQL 探活将跳过${NC}"
    echo -e "${YELLOW}      需要时执行: cp $PROJECT_DIR/.env.example $PROJECT_DIR/.env 并填写${NC}"
fi

echo -e "${YELLOW}=== 开始启动 Chat 项目 ===${NC}"
echo "项目目录: $PROJECT_DIR"
echo ""

# 1. 检查并启动 Redis
echo -e "${YELLOW}[1/5] 启动 Redis...${NC}"
if pgrep -f "redis-server" > /dev/null; then
    echo -e "${GREEN}Redis 已在运行${NC}"
else
    redis-server --daemonize yes
    sleep 1
    if pgrep -f "redis-server" > /dev/null; then
        echo -e "${GREEN}Redis 启动成功${NC}"
    else
        echo -e "${RED}Redis 启动失败${NC}"
        exit 1
    fi
fi

# 2. 启动 VerifyServer
echo -e "${YELLOW}[2/5] 启动 VerifyServer...${NC}"
cd $PROJECT_DIR/VerifyServer
if pgrep -f "node.*server.js" > /dev/null; then
    echo -e "${GREEN}VerifyServer 已在运行${NC}"
else
    nohup node server.js > $LOG_DIR/verify_server.log 2>&1 &
    sleep 2
    if pgrep -f "node.*server.js" > /dev/null; then
        echo -e "${GREEN}VerifyServer 启动成功 (PID: $(pgrep -f 'node.*server.js'))${NC}"
    else
        echo -e "${RED}VerifyServer 启动失败，查看日志: $LOG_DIR/verify_server.log${NC}"
        exit 1
    fi
fi

# 3. 启动 GateServer
echo -e "${YELLOW}[3/5] 启动 GateServer...${NC}"
cd $PROJECT_DIR/build/GateServer
if pgrep -f "./gateServer" > /dev/null; then
    echo -e "${GREEN}GateServer 已在运行${NC}"
else
    nohup ./gateServer > $LOG_DIR/gate_server.log 2>&1 &
    sleep 2
    if pgrep -f "./gateServer" > /dev/null; then
        echo -e "${GREEN}GateServer 启动成功 (PID: $(pgrep -f './gateServer'))${NC}"
    else
        echo -e "${RED}GateServer 启动失败，查看日志: $LOG_DIR/gate_server.log${NC}"
        exit 1
    fi
fi

# 4. 启动 ChatServer（支持多实例）
echo -e "${YELLOW}[4/5] 启动 ChatServer...${NC}"
cd $PROJECT_DIR/build/ChatServer

# 定义 ChatServer 实例列表：格式为 "实例名:配置文件路径:日志后缀"
# 日志后缀用于生成独立的日志文件，如 chat_server1.log、chat_server2.log
CHAT_SERVERS=(
    "ChatServer1:config.ini:1"
    "ChatServer2:config2.ini:2"
)

for server in "${CHAT_SERVERS[@]}"; do
    # 分割实例名、配置文件路径、日志后缀
    NAME="${server%%:*}"
    CONFIG="${server#*:}"
    CONFIG="${CONFIG%%:*}"
    LOG_SUFFIX="${server##*:}"
    
    echo -e "${YELLOW}正在启动 ${NAME}...${NC}"
    if pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
        echo -e "${GREEN}${NAME} 已在运行${NC}"
    else
        nohup ./chatServer "${CONFIG}" > "$LOG_DIR/chat_server${LOG_SUFFIX}.log" 2>&1 &
        sleep 2
        if pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
            PID=$(pgrep -f "./chatServer.*${CONFIG}")
            echo -e "${GREEN}${NAME} 启动成功 (PID: $PID)${NC}"
        else
            echo -e "${RED}${NAME} 启动失败，查看日志: $LOG_DIR/chat_server${LOG_SUFFIX}.log${NC}"
            exit 1
        fi
    fi
    echo ""
done

# 5. 启动 StatusServer
echo -e "${YELLOW}[5/5] 启动 StatusServer...${NC}"
cd $PROJECT_DIR/build/StatusServer
if pgrep -f "./statusServer" > /dev/null; then
    echo -e "${GREEN}StatusServer 已在运行${NC}"
else
    nohup ./statusServer > $LOG_DIR/status_server.log 2>&1 &
    sleep 2
    if pgrep -f "./statusServer" > /dev/null; then
        echo -e "${GREEN}StatusServer 启动成功 (PID: $(pgrep -f './statusServer'))${NC}"
    else
        echo -e "${RED}StatusServer 启动失败，查看日志: $LOG_DIR/status_server.log${NC}"
        exit 1
    fi
fi

echo ""
echo -e "${GREEN}=== 所有服务启动成功 ===${NC}"
echo ""
echo "服务状态:"
echo "  Redis:        127.0.0.1:6379"
echo "  VerifyServer: 127.0.0.1:50051"
echo "  GateServer:   端口 $(grep 'Port' $PROJECT_DIR/GateServer/config.ini | head -1 | awk -F'=' '{print $2}')"

# 动态显示每个 ChatServer 实例的端口
for server in "${CHAT_SERVERS[@]}"; do
    NAME="${server%%:*}"
    CONFIG="${server#*:}"
    CONFIG="${CONFIG%%:*}"
    PORT=$(grep 'Port' "$PROJECT_DIR/ChatServer/${CONFIG}" 2>/dev/null | head -1 | awk -F'=' '{print $2}' | tr -d ' ')
    if [ -n "$PORT" ]; then
        echo "  ${NAME}:     127.0.0.1:${PORT}"
    else
        echo "  ${NAME}:     配置文件未找到 Port 字段"
    fi
done

echo "  StatusServer: 127.0.0.1:50052"
echo ""
echo "日志文件:"
echo "  VerifyServer: $LOG_DIR/verify_server.log"
echo "  GateServer:   $LOG_DIR/gate_server.log"
# 动态显示每个 ChatServer 实例的日志文件
for server in "${CHAT_SERVERS[@]}"; do
    NAME="${server%%:*}"
    LOG_SUFFIX="${server##*:}"
    echo "  ${NAME}:     $LOG_DIR/chat_server${LOG_SUFFIX}.log"
done
echo "  StatusServer: $LOG_DIR/status_server.log"
echo ""
echo "停止服务: ./stop.sh"