import zmq
import time

# 连接到网关的 5555 端口
context = zmq.Context()
socket = context.socket(zmq.DEALER)
socket.connect("tcp://127.0.0.1:5555")

print(" 开始 LRU 极限压测...")

# 1. 模拟 1005 个新用户依次打招呼
# 你的 LRU 容量是 1000，这意味着前 5 个用户(user_0 到 user_4)的记忆会被挤掉
for i in range(1005):
    client_id = f"user_{i}".encode('utf-8')
    msg = b"Hello, I am user " + str(i).encode('utf-8')
    
    # 按照 ZeroMQ Router/Dealer 协议发送：[Identity, Empty, Data]
    socket.send(client_id, zmq.SNDMORE)
    socket.send(b"", zmq.SNDMORE)
    socket.send(msg)
    
    # 稍防止把 C++ 网关的接收缓冲区瞬间打满
    time.sleep(0.01) 

print(" 1005 个请求发送完毕，等待 C++ 网关处理 (请观察终端输出)...")

# 2. 验证淘汰：让 user_0 再次说话，应该看到“未命中缓存”
time.sleep(10) # 等网关处理完
print("\n 验证环节：User 0 再次请求 (预期结果：它的记忆已经被淘汰了)")
socket.send(b"user_0", zmq.SNDMORE)
socket.send(b"", zmq.SNDMORE)
socket.send(b"Do you remember me?")

# 3. 验证命中与保活：让 user_1000 再次说话，应该看到“命中缓存”
time.sleep(2)
print("\n 验证环节：User 1000 再次请求 (预期结果：命中缓存，排在链表最前面)")
socket.send(b"user_1000", zmq.SNDMORE)
socket.send(b"", zmq.SNDMORE)
socket.send(b"Do you remember me?")

# 保持脚本运行，接收网关的回包（防止报错）
for i in range(1007):
    socket.recv_multipart()