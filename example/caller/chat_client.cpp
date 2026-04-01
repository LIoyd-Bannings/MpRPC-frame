#include <zmq.hpp>
#include <string>
#include <iostream>

// ANSI 颜色宏，让客户端也骚气起来
#define ANSI_RES "\033[0m"
#define ANSI_CYA "\033[1;36m"
#define ANSI_PUR "\033[1;35m"
#define ANSI_GRE "\033[1;32m"

int main(int argc, char** argv) {
    // 1. 初始化 ZeroMQ
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req); // REQ 模式：发一个请求，等一个回复
    
    std::cout << ANSI_CYA "正在连接到 Lele 的分布式 AI 网关..." ANSI_RES << std::endl;
    socket.connect("tcp://127.0.0.1:5555");
    
    std::string client_name = (argc > 1) ? argv[1] : "智能林弟";
    std::cout << ANSI_GRE "连接成功！欢迎，" << client_name << "。你可以开始提问了 (输入 exit 退出)。" ANSI_RES << "\n";

    // 2. 熟悉的交互式死循环回来啦！
    while (true) {
        std::string user_input;
        std::cout << "\n" ANSI_PUR "[" << client_name << "] > " ANSI_RES;
        std::getline(std::cin, user_input);

        if (user_input == "exit" || user_input == "quit") {
            std::cout << "退出群聊，拜拜！\n";
            break;
        }
        if (user_input.empty()) continue;

        // 3. 把你敲的字通过 ZeroMQ 发给后台网关
        socket.send(zmq::buffer(user_input), zmq::send_flags::none);

        // 4. 等待网关的 16 个线程算完并把结果传回来
        zmq::message_t reply;
        socket.recv(reply, zmq::recv_flags::none);

        std::cout << ANSI_CYA "[凡哥 AI] > " ANSI_RES << reply.to_string() << std::endl;
    }

    return 0;
}