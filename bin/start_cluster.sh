#!/bin/bash

echo "[清理阶段] 正在杀掉所有旧的 Provider 进程..."
# -9 强制杀死所有名为 agent_provider 的进程，确保端口释放
ps -ef | grep agent_provider | grep -v grep | awk '{print $2}' | xargs kill -9 2>/dev/null

# 给系统 1 秒钟时间彻底回收端口
sleep 1

echo "[点火阶段] 正在启动纯异步 RPC 物理节点集群..."

# 启动最新的 ZMQ 节点，并将日志实时输出
./agent_provider -i test_8001.conf > provider1.log 2>&1 &
./agent_provider -i test_8002.conf > provider2.log 2>&1 &
./agent_provider -i test_8003.conf > provider3.log 2>&1 &

echo " 集群启动完毕！3 个纯异步节点已进入就绪状态。"
echo "可以使用 'tail -f provider1.log' 查看最新 ZMQ 路由日志。"