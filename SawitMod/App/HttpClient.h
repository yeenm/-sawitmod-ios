#pragma once
// ============================================================================
//  HttpClient.h — 同步 HTTP（替代 Java HttpURLConnection）
// ----------------------------------------------------------------------------
//  与 Android 版语义对齐：
//    * 面板 HTTPS 走 DNS/Cloudflare，客户端不内置源站 IP
//    * 网络异常/5xx 原样返回；有效 4xx/业务拒绝原样返回；单次最多 8 秒
//    * 非 2xx 也把响应体读回来（服务器在 401/403 里带签名 JSON）
//    * 失败返回 transportOk=false + 空 body，调用方按"网络错误"处理
//  只能在后台线程调用：内部用信号量阻塞，主线程调用会卡界面。
// ============================================================================

#include <string>

namespace SMHttp {

struct Response {
    long code = 0;
    std::string body;
    bool transportOk = false;
};

Response Post(const std::string &url, const std::string &body,
              const std::string &contentType = "application/json",
              int timeoutSec = 8);
Response Get(const std::string &url, int timeoutSec = 8);

// 便捷：只要 body（失败返回空串），与 Android 的 HttpPostNative 行为一致。
std::string PostJson(const std::string &url, const std::string &json, int timeoutSec = 8);

}  // namespace SMHttp
