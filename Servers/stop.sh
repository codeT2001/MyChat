#!/bin/bash

# Chat项目停止脚本

# 动态获取脚本所在目录（Servers/），从任意路径调用都能正确定位
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== 停止 Chat 项目 ===${NC}"
echo ""

# 1. 停止 GateServer（先停网关，切断外部流量）
echo -e "${YELLOW}[1/4] 停止 GateServer...${NC}"
if pgrep -f "./gateServer" > /dev/null; then
    pkill -f "./gateServer"

    # 等待最多1分钟让GateServer优雅停止
    echo -e "${YELLOW}等待 GateServer 优雅停止（最多1分钟）...${NC}"
    WAIT_TIME=60
    while [ $WAIT_TIME -gt 0 ] && pgrep -f "./gateServer" > /dev/null; do
        echo -n "."
        sleep 1
        WAIT_TIME=$((WAIT_TIME - 1))
    done
    echo ""

    if pgrep -f "./gateServer" > /dev/null; then
        echo -e "${RED}GateServer 未在1分钟内停止，强制终止${NC}"
        pkill -9 -f "./gateServer"
        sleep 1
        if pgrep -f "./gateServer" > /dev/null; then
            echo -e "${RED}GateServer 停止失败${NC}"
        else
            echo -e "${GREEN}GateServer 已强制停止${NC}"
        fi
    else
        echo -e "${GREEN}GateServer 已优雅停止${NC}"
    fi
else
    echo -e "${GREEN}GateServer 未运行${NC}"
fi
echo ""

# 2. 停止 ChatServer（支持多实例）
echo -e "${YELLOW}[2/4] 停止 ChatServer...${NC}"
CHAT_SERVERS=(
    "ChatServer1:config.ini"
    "ChatServer2:config2.ini"
)

for server in "${CHAT_SERVERS[@]}"; do
    NAME="${server%%:*}"
    CONFIG="${server##*:}"
    
    echo -e "${YELLOW}正在停止 ${NAME}...${NC}"
    
    # 先查，查不到就直接跳过
    if ! pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
        echo -e "${GREEN}${NAME} 未运行，跳过${NC}"
        continue
    fi
    
    # 查到了再杀（与 GateServer 一致，最多等待 60 秒优雅退出，
    # ChatServer 退出链长：停 IO 线程池 + 关 gRPC + 清 Redis + 单例析构）
    pkill -f "./chatServer.*${CONFIG}"
    WAIT_TIME=60
    while [ $WAIT_TIME -gt 0 ] && pgrep -f "./chatServer.*${CONFIG}" > /dev/null; do
        echo -n "."
        sleep 1
        WAIT_TIME=$((WAIT_TIME - 1))
    done
    echo ""

    if pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
        echo -e "${YELLOW}${NAME} 未响应 SIGTERM，强制终止...${NC}"
        pkill -9 -f "./chatServer.*${CONFIG}"
        sleep 1
        
        if pgrep -f "./chatServer.*${CONFIG}" > /dev/null; then
            echo -e "${RED}${NAME} 停止失败${NC}"
        else
            echo -e "${GREEN}${NAME} 已强制停止${NC}"
        fi
    else
        echo -e "${GREEN}${NAME} 已优雅停止${NC}"
    fi
    echo ""
done

# 3. 停止 StatusServer
echo -e "${YELLOW}[3/4] 停止 StatusServer...${NC}"
if pgrep -f "./statusServer" > /dev/null; then
    pkill -f "./statusServer"
    sleep 1
    if pgrep -f "./statusServer" > /dev/null; then
        echo -e "${RED}StatusServer 停止失败${NC}"
    else
        echo -e "${GREEN}StatusServer 已停止${NC}"
    fi
else
    echo -e "${GREEN}StatusServer 未运行${NC}"
fi
echo ""

# 4. 停止 VerifyServer
echo -e "${YELLOW}[4/4] 停止 VerifyServer...${NC}"
if pgrep -f "node.*server.js" > /dev/null; then
    pkill -f "node.*server.js"
    sleep 1
    if pgrep -f "node.*server.js" > /dev/null; then
        echo -e "${RED}VerifyServer 停止失败${NC}"
    else
        echo -e "${GREEN}VerifyServer 已停止${NC}"
    fi
else
    echo -e "${GREEN}VerifyServer 未运行${NC}"
fi
echo ""

echo -e "${GREEN}=== 所有服务已停止 ===${NC}"