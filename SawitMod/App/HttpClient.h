#pragma once
// ============================================================================
//  HttpClient.h — 同步 HTTP（替代 Java HttpURLConnection）
// ----------------------------------------------------------------------------
//  与 Android 版语义对齐：
//    * 面板 HTTPS 保留域名证书/SNI，主节点 216.167.14.42，备用 156.239.4.155
//    * 网络异常/5xx 切换节点；有效 4xx/业务拒绝原样返回；每节点最多 5 秒
//    * 备用节点短暂优先 30 秒，随后重新优先主节点
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
