#pragma once
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <sstream>
#include <jwt-cpp/jwt.h>
#include "RedisUtil.h"

// 声明外部的鉴权函数和密钥 (确保你的网关里有这些)
extern bool VerifyUserFromDB(const std::string& username, const std::string& password);
const std::string JWT_SECRET = "Lele_Super_Secret_2026_RPC"; 

// ==========================================
//  1. 请求上下文 (贯穿全链路的包裹)
// ==========================================
struct RequestContext {
    std::string client_id;      // ZMQ 的 Identity
    std::string raw_input;      // 用户最原始的输入流
    
    std::string real_question;  // 剥离掉指令和 Token 后的纯净提问
    std::string response_msg;   // 如果被拦截，或者直接处理完了，放在这里
    bool is_handled;            // 【关键】如果为 true，说明已经处理完毕，网关直接返回 response_msg，不再交给大模型
    
    RequestContext(std::string id, std::string input) 
        : client_id(id), raw_input(input), is_handled(false) {}
};

// ==========================================
//  2. 安检门基类 (Filter)
// ==========================================
class Filter {
public:
    virtual ~Filter() = default;
    // 返回 true 表示放行给下一个门；返回 false 表示拦截/终止
    virtual bool doFilter(RequestContext& ctx) = 0; 
};

// ==========================================
// 🛂 安检门 A：登录处理器 (LoginFilter)
// ==========================================
class LoginFilter : public Filter {
public:
    bool doFilter(RequestContext& ctx) override {
        if (ctx.raw_input.find("login ") == 0) {
            std::istringstream iss(ctx.raw_input);
            std::string cmd, user, pwd;
            iss >> cmd >> user >> pwd;

            if (VerifyUserFromDB(user, pwd)) {
                auto token = jwt::create()
                    .set_issuer("lele_rpc_gateway")
                    .set_type("JWS")
                    .set_payload_claim("user", jwt::claim(user))
                    .set_issued_at(std::chrono::system_clock::now())
                    .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(2))
                    .sign(jwt::algorithm::hs256{JWT_SECRET});

                ctx.response_msg = " 登录成功！你的专属 Token 是:\n" + token;
            } else {
                ctx.response_msg = " 401 Unauthorized: 账号或密码错误！";
            }
            ctx.is_handled = true; // 告诉网关：登录流程已走完，不用给大模型了
            return false; // 中断后续安检门
        }
        return true; // 不是 login 指令，放行给下一个安检门
    }
};

// ==========================================
// 🛂 安检门 B：JWT 验票与黑名单处理器 (JwtAuthFilter)
// ==========================================
class JwtAuthFilter : public Filter {
public:
    bool doFilter(RequestContext& ctx) override {
        if (ctx.raw_input.find("Bearer ") != 0) {
            ctx.response_msg = " 401 Unauthorized: 查无门票！请先 login。后续发言格式: 'Bearer <Token> 问题'";
            ctx.is_handled = true;
            return false;
        }

        try {
            size_t first_space = ctx.raw_input.find(' ', 7);
            if (first_space == std::string::npos) throw std::runtime_error("格式错误，缺少操作内容");

            std::string token_str = ctx.raw_input.substr(7, first_space - 7);
            std::string content = ctx.raw_input.substr(first_space + 1);

            // 1. 查 Redis 黑名单
            if (RedisUtil::getInstance()->exists("BL:" + token_str)) {
                throw std::runtime_error("该 Token 已被主动登出注销，请重新 login！");
            }

            // 2. 验签名和过期时间
            auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{JWT_SECRET}).with_issuer("lele_rpc_gateway");
            auto decoded = jwt::decode(token_str);
            verifier.verify(decoded); 

            // 3. 剥离伪装，提取真身
            ctx.real_question = content; 
            // 顺便把 token_str 临时塞进上下文，方便下一个 LogoutFilter 用
            ctx.raw_input = token_str; 

            printf("\033[1;36m[网关安检] 鉴权通过！放行用户: %s\033[0m\n", decoded.get_payload_claim("user").as_string().c_str());
            return true; // 门票合法，放行！

        } catch (const std::exception& e) {
            ctx.response_msg = std::string(" 403 Forbidden: 门票无效或已过期 (") + e.what() + ")";
            ctx.is_handled = true;
            return false;
        }
    }
};

// ==========================================
// 🛂 安检门 C：登出拦截器 (LogoutFilter)
// ==========================================
class LogoutFilter : public Filter {
public:
    bool doFilter(RequestContext& ctx) override {
        // 注意：经过上一个门，ctx.real_question 已经是纯净的指令了
        if (ctx.real_question == "logout") {
            // 上一个门我们巧妙地把 token 存在了 raw_input 里
            std::string token = ctx.raw_input; 
            
            bool success = RedisUtil::getInstance()->setEx("BL:" + token, "dead", 7200);
            ctx.response_msg = success ? " 登出成功！您的账号已安全下线，Token 已作废。" : " 500 系统错误：Redis 写入失败。";
            ctx.is_handled = true;
            return false; // 处理完毕，中断流水线
        }
        return true; // 是正常的聊天内容，全部放行！
    }
};

// ==========================================
//  3. 流水线引擎 (Pipeline Manager)
// ==========================================
class GatewayPipeline {
private:
    std::vector<std::shared_ptr<Filter>> filters;
public:
    GatewayPipeline() {
        // 核心装配：按严格的顺序装配安检门
        filters.push_back(std::make_shared<LoginFilter>());
        filters.push_back(std::make_shared<JwtAuthFilter>());
        filters.push_back(std::make_shared<LogoutFilter>());
    }

    void process(RequestContext& ctx) {
        for (auto& filter : filters) {
            // 如果某个门返回 false，立刻终止流水线
            if (!filter->doFilter(ctx)) {
                break; 
            }
        }
    }
};