#pragma once

#include <string>

// =====================================================================
//  微验（weiyan）卡密验证
// =====================================================================
// 移植自 "C++ v2-已注入" 的 weiyan/Util.h + main.cpp，协议保持逐字节一致：
//
//   host  wy.llua.cn        path  v2/c84ec01e30ad49897de7c59f5edd4af8   （HTTP/80）
//   请求体 = hex( RC4( 明文, KEY ) )
//   响应体 = RC4( hex解码(body), KEY ) -> JSON
//
//   端点（靠 id= 区分）
//     CAE73D714   卡密登录   kami / markcode / t / sign / value
//     5DD7BD45C4C 服务器公告 -> msg.app_gg
//     7D46A8F7D   版本信息   -> msg.version / updateshow / updateurl / updatemust
//
//   签名 sign = md5("kami=<卡密>&markcode=<设备码>&t=<时间戳>&" + SALT)
//
// 网络走原生 socket（不用 Java 网络栈），因此不受 host app 的
// usesCleartextTraffic 策略限制 —— 注入进程里 http:// 同样能通。
namespace Weiyan {

    struct Result {
        bool ok = false;
        std::string message;      // 给用户看的中文提示
        std::string ktype;        // single（次数卡）/ vip（时间卡）/ 空=面板未给
        int remaining = -1;       // 单码剩余可登录次数
        long long expiresAt = 0;  // 时间卡到期 unix 秒
        long long durationSec = 0;// 面板若给的是"剩余时长"就放这里
        std::string raw;          // 服务器解出来的原始 JSON（排查用）
    };

    // ---- 会阻塞的网络调用，必须在后台线程里跑 ----
    Result VerifyCard(const std::string& cardKey);
    std::string FetchNotice();                 // 服务器公告正文（失败返回空串 + 记 lastError）
    bool FetchUpdateInfo(std::string& version, std::string& show,
                         std::string& url, bool& must);

    // ---- 接口配置（面板换过接口 id / 应用key 时不用重编，改配置文件即可）----
    // 默认值来自 zip 里的 main.cpp：host=wy.llua.cn、path=v2/c84ec0…、login id=CAE73D714
    std::string LoginId();
    std::string ApiHost();
    std::string ApiPath();
    std::string LoginPath();
    std::string AppId();
    // 走面板文档的 /api/?id=kmlogon&app=<应用ID>&data=<密文> 接口（默认 true）；
    // false = 老式 POST /v2/<token> + id=<接口ID>
    bool UseDocLogin();
    std::string BuildDocLoginUrl(const std::string& params);   // 自检/日志用
    // 传空串表示该字段沿用默认值；写入 SMPlatform::DataDir()/weiyan_api.txt
    bool SetApiConfig(const std::string& host, const std::string& path,
                      const std::string& loginId, const std::string& loginPath,
                      const std::string& appId);

    // ---- 登录状态（UI 只读）----
    bool IsVerified();
    std::string StatusMessage();
    std::string CurrentCard();
    std::string LastRawResponse();
    // 卡密面板返回的有效期：到期时间（unix 秒）/ 剩余次数 / 剩余时长（秒）
    long long CardExpiresAt();
    int CardRemaining();
    long long CardDurationSec();
    std::string CardExpiryText();   // "2027-01-01 00:00:00" / "剩余 30 天" / ""
    void SetStatus(const std::string& message);
    void Logout();

    // ---- 卡密与设备码 ----
    std::string LoadSavedCard();
    bool SaveCard(const std::string& key);
    std::string DeviceCode();

    // ---- 自检（供设备侧回归用）----
    bool SelfTest(std::string& report);
}
