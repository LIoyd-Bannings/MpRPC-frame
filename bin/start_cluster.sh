#!/bin/bash

echo "正在启动 RPC 物理节点集群..."

# 使用后台运行符 &，一键拉起三个进程！
./agent_provider -i test_8001.conf > provider1.log 2>&1 &
./agent_provider -i test_8002.conf > provider2.log 2>&1 &
./agent_provider -i test_8003.conf > provider3.log 2>&1 &

echo "集群启动完毕！共拉起 3 个 Provider 节点。"
echo "可以使用 'ps -ef | grep provider' 查看进程状态。"