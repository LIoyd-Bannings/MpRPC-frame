#include <iostream>
#include <string>
// 【关键】必须在 include httplib.h 之前定义这个宏，开启 HTTPS 支持！
#define CPPHTTPLIB_OPENSSL_SUPPORT 
#include "httplib.h"
#include "json.hpp" // 引入 nlohmann json

using json = nlohmann::json;

int main() {
    // 1. 初始化 HTTP 客户端，直连 DeepSeek 的公网服务器
    httplib::Client cli("https://api.deepseek.com");
    cli.set_read_timeout(30, 0); // 大模型思考需要时间，设置 30 秒网络超时防卡死

    // 2. 准备 HTTP 请求头 (在这里填入 API Key)
    // 工业规范：凭证必须放在 Authorization 头里，用 Bearer 携带
    httplib::Headers headers = {
        {"Authorization", "Bearer sk-0cc31c9c975c449aafcea1846040f2af"},
        {"Content-Type", "application/json"}
    };

    // 3. 构造发给大模型的 JSON 数据包 (标准的 OpenAI 聊天格式)
    json request_body = {
        {"model", "deepseek-chat"}, // 指定模型
        {"messages", {
            // role: user 表示人类发问
            {{"role", "user"}, {"content", "你好，请你用一句话向我解释一下什么是 C++ 的 RPC？"}}
        }},
        {"temperature", 0.7} // 控制回答的发散程度
    };

    std::cout << "正在呼叫 DeepSeek  跨越公网中..." << std::endl;

    // 4. 发起 HTTP POST 请求！
    auto res = cli.Post("/chat/completions", headers, request_body.dump(), "application/json");

    // 5. 极其严谨的底层响应处理
    if (res) {
        if (res->status == 200) { // HTTP 200 代表绝对成功
            // 解析大模型吐回来的复杂 JSON
            json response_json = json::parse(res->body);
            // 顺藤摸瓜，扒出最终的回答文本
            std::string ai_reply = response_json["choices"][0]["message"]["content"];
            
            std::cout << "\n========== DeepSeek 回复 ==========\n";
            std::cout << ai_reply << std::endl;
            std::cout << "=======================================\n";
        } else {
            // 如果返回 401，说明 API Key 写错了；400说明 JSON 拼错了
            std::cout << "HTTP 请求失败，状态码: " << res->status << std::endl;
            std::cout << "大厂接口报错明细: " << res->body << std::endl;
        }
    } else {
        // 底层网络彻底断开（比如没网了，或者没装 SSL）
        auto err = res.error();
        std::cout << "网络连接彻底溃散，httplib 错误码: " << httplib::to_string(err) << std::endl;
    }

    return 0;
}