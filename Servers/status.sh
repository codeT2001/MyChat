#!/bin/bash

# Chat项目状态检查脚本

# 动态获取脚本所在目录（Servers/），从任意路径调用都能正确定位
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$PROJECT_DIR/logs"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 从 .env 加载 MySQL 凭据，避免口令硬编码在脚本中（.env 不入库，模板见 .env.example）
ENV_FILE="$PROJECT_DIR/.env"
if [ -f "$ENV_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    set +a
fi

echo "=========================================="
echo "    Chat 项目运行状态"
echo "=========================================="
echo ""

# 检查 StatusServer
echo -e "${YELLOW}StatusServer:${NC}"
if pgrep -f "./statusServer" > /dev/null; then
    PID=$(pgrep -f "./statusServer")
    echo "  状态: ${GREEN}运行中${NC}"
    echo "  PID: $PID"
    echo "  端口: 50052"
    echo "  内存: $(ps -p $PID -o rss= | awk '{print int($1/1024) " MB"}')"
else
    echo "  状态: ${YELLOW}未运行${NC}"
fi
echo ""

# 检查 ChatServer（支持多实例）
echo -e "${YELLOW}ChatServer:${NC}"
# 定义 ChatServer 实例列表：格式为 "实例名:配置文件路径"
CHAT_SERVERS=(
    "ChatServer1:config.ini"
    "ChatServer2:config2.ini"
)

for server in "${CHAT_SERVERS[@]}"; do
    # 分割实例名和配置文件路径
    NAME="${server%%:*}"
    CONFIG="${server##*:}"
    
    # 通过配置文件名精准匹配进程
    if pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
        PID=$(pgrep -f "./chatServer.*${CONFIG}")
        echo "  ${NAME}:"
        echo "    状态: ${GREEN}运行中${NC}"
        echo "    PID: $PID"
        echo "    配置: ${CONFIG}"
        # 从配置文件中读取端口号
        PORT=$(grep 'Port' "$PROJECT_DIR/ChatServer/${CONFIG}" 2>/dev/null | head -1 | awk -F'=' '{print $2}' | tr -d ' ')
        if [ -n "$PORT" ]; then
            echo "    端口: $PORT"
        fi
        echo "    内存: $(ps -p $PID -o rss= | awk '{print int($1/1024) " MB"}')"
    else
        echo "  ${NAME}:"
        echo "    状态: ${YELLOW}未运行${NC}"
        echo "    配置: ${CONFIG}"
    fi
    echo ""
done

# 检查 GateServer
echo -e "${YELLOW}GateServer:${NC}"
if pgrep -f "./gateServer" > /dev/null; then
    PID=$(pgrep -f "./gateServer")
    echo "  状态: ${GREEN}运行中${NC}"
    echo "  PID: $PID"
    PORT=$(grep 'Port' $PROJECT_DIR/GateServer/config.ini | head -1 | awk -F'=' '{print $2}')
    echo "  端口: $PORT"
    echo "  内存: $(ps -p $PID -o rss= | awk '{print int($1/1024) " MB"}')"
else
    echo "  状态: ${YELLOW}未运行${NC}"
fi
echo ""

# 检查 VerifyServer
echo -e "${YELLOW}VerifyServer:${NC}"
if pgrep -f "node.*server.js" > /dev/null; then
    PID=$(pgrep -f "node.*server.js")
    echo "  状态: ${GREEN}运行中${NC}"
    echo "  PID: $PID"
    echo "  内存: $(ps -p $PID -o rss= | awk '{print int($1/1024) " MB"}')"
else
    echo "  状态: ${YELLOW}未运行${NC}"
fi
echo ""

# 检查 Redis
echo -e "${YELLOW}Redis:${NC}"
if pgrep -f "redis-server" > /dev/null; then
    echo "  状态: ${GREEN}运行中${NC}"
    REDIS_PORT=$(redis-cli INFO server | grep tcp_port | awk -F':' '{print $2}')
    echo "  端口: $REDIS_PORT"
else
    echo "  状态: ${YELLOW}未运行${NC}"
fi
echo ""

# 检查 MySQL（凭据来自 .env：MYSQL_USER/MYSQL_PASSWORD，主机/端口可选，默认本机）
echo -e "${YELLOW}MySQL:${NC}"
if [ -z "$MYSQL_USER" ] || [ -z "$MYSQL_PASSWORD" ]; then
    echo "  状态: ${YELLOW}跳过（.env 未配置 MYSQL_USER/MYSQL_PASSWORD）${NC}"
elif command -v mysql &> /dev/null; then
    if mysql -h"${MYSQL_HOST:-127.0.0.1}" -P"${MYSQL_PORT:-3306}" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" -e "SELECT 1" &> /dev/null; then
        echo "  状态: ${GREEN}运行中${NC}"
    else
        echo "  状态: ${YELLOW}连接失败${NC}"
    fi
else
    echo "  状态: ${YELLOW}未安装${NC}"
fi
echo ""

# 日志文件
echo -e "${YELLOW}日志文件:${NC}"
if [ -f "$LOG_DIR/status_server.log" ]; then
    echo "  StatusServer: $LOG_DIR/status_server.log"
    LINES=$(tail -n 5 "$LOG_DIR/status_server.log" | wc -l)
    echo "    最近 $LINES 行"
fi
if [ -f "$LOG_DIR/chat_server.log" ]; then
    echo "  ChatServer: $LOG_DIR/chat_server.log"
    LINES=$(tail -n 5 "$LOG_DIR/chat_server.log" | wc -l)
    echo "    最近 $LINES 行"
fi
if [ -f "$LOG_DIR/verify_server.log" ]; then
    echo "  VerifyServer: $LOG_DIR/verify_server.log"
    LINES=$(tail -n 5 "$LOG_DIR/verify_server.log" | wc -l)
    echo "    最近 $LINES 行"
fi
if [ -f "$LOG_DIR/gate_server.log" ]; then
    echo "  GateServer: $LOG_DIR/gate_server.log"
    LINES=$(tail -n 5 "$LOG_DIR/gate_server.log" | wc -l)
    echo "    最近 $LINES 行"
fi

echo ""
echo "=========================================="