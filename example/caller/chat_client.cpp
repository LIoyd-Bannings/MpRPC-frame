#include <zmq.hpp>
#include <string>
#include <iostream>

// ANSI 颜色宏，让客户端也骚气起来
#define ANSI_RES "\033[0m"
#define ANSI_CYA "\033[1;36m"
#define ANSI_PUR "\033[1;35m"
#define ANSI_GRE "\033[1;32m"
#define ANSI_RED "\033[1;31m"
#define ANSI_YEL "\033[1;33m"

int main(int argc, char** argv) {
    // 1. 初始化 ZeroMQ
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req); // REQ 模式
    
    std::string client_name = (argc > 1) ? argv[1] : "智能林弟";
    std::cout << ANSI_CYA "正在连接到 Lele 的分布式 AI 网关..." ANSI_RES << std::endl;
    socket.connect("tcp://127.0.0.1:5556");
    std::cout << ANSI_GRE "连接成功！欢迎，" << client_name << "。" ANSI_RES << "\n";

    std::string current_token = ""; // 全局状态：存放门票

    // 🌟 外层主循环：控制客户端整个生命周期
    while (true) {
        
        // ==========================================
        // 🚪 状态一：未登录阶段 (只允许发 login)
        // ==========================================
        while (current_token.empty()) {
            std::string username, password;
            std::cout << "\n" ANSI_YEL "👤 请输入账号: " ANSI_RES;
            std::getline(std::cin, username);
            std::cout << ANSI_YEL "🔑 请输入密码: " ANSI_RES;
            std::getline(std::cin, password);

            // 如果用户在这里输入 exit，直接彻底退出
            if (username == "exit" || username == "quit") {
                std::cout << "退出群聊，拜拜！\n";
                return 0; 
            }

            // 发送纯净的登录指令
            std::string login_cmd = "login " + username + " " + password;
            socket.send(zmq::buffer(login_cmd), zmq::send_flags::none);

            zmq::message_t reply;
            socket.recv(reply, zmq::recv_flags::none);
            std::string reply_str = reply.to_string();

            std::cout << "\n[系统] " << reply_str << "\n";

            if (reply_str.find("登录成功") != std::string::npos) {
                size_t pos = reply_str.find_last_of('\n');
                if (pos != std::string::npos) {
                    current_token = reply_str.substr(pos + 1); // 提取保存 Token
                    std::cout << ANSI_GRE "🎉 登录成功！已自动保存安全凭证，开始畅聊吧！(输入 exit 退出客户端，输入 logout 注销账号)" ANSI_RES << "\n";
                    break; //  拿到门票，打破未登录循环，进入下面的聊天循环！
                }
            } else {
                std::cout << ANSI_RED "⚠️ 登录失败，请检查账号密码并重试。" ANSI_RES "\n";
            }
        }

        // ==========================================
        // 💬 状态二：已登录阶段 (正常聊天)
        // ==========================================
        while (!current_token.empty()) {
            std::string user_input;
            std::cout << "\n" ANSI_PUR "[" << client_name << "] > " ANSI_RES;
            std::getline(std::cin, user_input);

            if (user_input == "exit" || user_input == "quit") {
                std::cout << "退出群聊，拜拜！\n";
                return 0; // 彻底关掉客户端进程
            }
            if (user_input.empty()) continue;

            // 核心魔法：自动拼装门票发送
            std::string auth_request = "Bearer " + current_token + " " + user_input;
            socket.send(zmq::buffer(auth_request), zmq::send_flags::none);

            zmq::message_t reply;
            socket.recv(reply, zmq::recv_flags::none);
            std::string reply_str = reply.to_string();

            std::cout << ANSI_CYA "[凡哥 AI] > " ANSI_RES << reply_str << std::endl;

            // ==========================================
            // 🚀 核心状态自愈：监听服务端的死亡宣告
            // ==========================================
            if (reply_str.find("登出成功") != std::string::npos || 
                reply_str.find("403 Forbidden") != std::string::npos) {
                
                // 1. 物理销毁本地失效的门票
                current_token = ""; 
                
                // 2. UI 提示
                std::cout << "\n========================================" << std::endl;
                std::cout << ANSI_YEL " ⚠️ 登录状态已失效，已安全退出。" ANSI_RES << std::endl;
                std::cout << ANSI_YEL " 请重新登录以获取新的通行证。" ANSI_RES << std::endl;
                std::cout << "========================================\n" << std::endl;
                
                break; // 门票被毁，打破聊天循环。程序会自动回到上面的【状态一】！
            }
        }
    }

    return 0;
}