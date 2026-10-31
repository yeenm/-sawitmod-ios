// ============================================================================
//  ChatRoom.mm — iOS 客户端聊天室实现（Objective-C++）
// ----------------------------------------------------------------------------
//  对接：面板 http://yslf.work 的 /api/chat/list 与 /api/chat/send（HTTP 80）。
//  游戏 Info.plist 已配置 NSAllowsArbitraryLoads=true，所以 NSURLSession 走明文
//  HTTP 不受 ATS 影响（与参考实现一致）。
//
//  卡密/设备码来源：Weiyan::CurrentCard() / Weiyan::DeviceCode()
//    —— 必须与卡密登录时一致，否则服务器找不到在线会话会返回 403。
//  服务器地址：Weiyan::ApiHost()（面板换域名时改 Weiyan 配置即可，这里自动跟随）。
//
//  线程：Tick()/ShowInputDialog()/Send() 都在主线程（ImGui 渲染线程）调用；
//        NSURLSession 完成回调在后台队列，状态更新全部加锁。
// ============================================================================

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "App/ChatRoom.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "Core/State.h"
#include "Weiyan.h"
#include "NetAuth.h"

namespace SMChatRoom {
namespace {

// ---------------------------------------------------------------- 基础状态
std::mutex g_mtx;
Snapshot g_snap;
std::vector<Message> g_messages;
long long g_lastId = 0;
uint64_t g_lastFetchMs = 0;
bool g_inFlight = false;
bool g_forceReload = false;

uint64_t NowMs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string StdString(NSString *s) {
    return s ? std::string([s UTF8String] ? [s UTF8String] : "") : std::string();
}

NSString *NsString(const std::string &s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

// 只允许 RFC3986 unreserved 字符，避免卡密/设备码里的字符破坏 query。
NSString *UrlEncode(NSString *s) {
    if (!s) return @"";
    NSMutableCharacterSet *set = [[NSMutableCharacterSet alloc] init];
    [set formUnionWithCharacterSet:[NSCharacterSet alphanumericCharacterSet]];
    [set addCharactersInString:@"-._~"];
    return [s stringByAddingPercentEncodingWithAllowedCharacters:set] ?: @"";
}

NSString *ChatBaseUrl() {
    std::string host = Weiyan::ApiHost();
    if (host.empty()) host = "yslf.work";
    if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0) return NsString(host);
    return NsString("http://" + host);
}

NSURLSession *ChatSession() {
    static NSURLSession *session = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = 12.0;
        cfg.timeoutIntervalForResource = 20.0;
        cfg.HTTPShouldUsePipelining = NO;
        cfg.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        session = [NSURLSession sessionWithConfiguration:cfg];
    });
    return session;
}

bool ChatCredentials(std::string &license, std::string &hwid) {
    license = Weiyan::CurrentCard();
    hwid = Weiyan::DeviceCode();
    return !license.empty() && !hwid.empty();
}

void SetErrorLocked(const std::string &error) {
    g_snap.error = error;
}

void ApplyListResponse(NSDictionary *obj, bool fullReload) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_snap.loading = false;
    if (![obj isKindOfClass:[NSDictionary class]]) {
        SetErrorLocked("响应解析失败");
        return;
    }
    if (![obj[@"ok"] boolValue]) {
        NSString *err = obj[@"error"];
        SetErrorLocked(err ? StdString(err) : std::string("请求失败"));
        return;
    }
    g_snap.error.clear();
    if (obj[@"me"]) g_snap.me = StdString(obj[@"me"]);
    g_snap.online = [obj[@"online"] intValue];

    NSArray *msgs = obj[@"messages"];
    if (fullReload) {
        g_messages.clear();
        g_lastId = 0;
    }
    if ([msgs isKindOfClass:[NSArray class]]) {
        for (NSDictionary *m in msgs) {
            if (![m isKindOfClass:[NSDictionary class]]) continue;
            Message msg;
            msg.id = [m[@"id"] longLongValue];
            msg.user = StdString(m[@"user"] ?: @"");
            msg.text = StdString(m[@"text"] ?: @"");
            msg.kind = m[@"kind"] ? StdString(m[@"kind"]) : "user";
            msg.ts = [m[@"ts"] longLongValue];
            if (msg.id <= 0) continue;
            if (msg.id <= g_lastId && !fullReload) continue;   // 增量去重
            g_messages.push_back(msg);
            if (msg.id > g_lastId) g_lastId = msg.id;
        }
    }
    // 只保留最近 300 条，避免长时间挂机内存膨胀
    if (g_messages.size() > 300) {
        g_messages.erase(g_messages.begin(), g_messages.begin() + (long) (g_messages.size() - 300));
    }
    g_snap.messages = g_messages;
}

void DoListFetch(bool fullReload) {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_inFlight) return;
        g_inFlight = true;
        g_snap.loading = true;
        g_forceReload = fullReload;
    }
    std::string license, hwid;
    if (!ChatCredentials(license, hwid)) {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_inFlight = false;
        g_snap.loading = false;
        g_snap.available = false;
        SetErrorLocked("请先在「主页」登录卡密");
        return;
    }
    const long long after = fullReload ? 0 : g_lastId;
    NSString *urlStr = [NSString stringWithFormat:@"%@/api/chat/list?license=%@&hwid=%@&after=%lld&limit=50",
                        ChatBaseUrl(),
                        UrlEncode(NsString(license)),
                        UrlEncode(NsString(hwid)),
                        after];
    NSURL *url = [NSURL URLWithString:urlStr];
    if (!url) {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_inFlight = false;
        g_snap.loading = false;
        SetErrorLocked("URL 无效");
        return;
    }
    NSURLSessionDataTask *task = [ChatSession() dataTaskWithURL:url
        completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSDictionary *obj = nil;
            if (!error && data.length > 0) {
                obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                ApplyListResponse(obj, fullReload);
                std::lock_guard<std::mutex> lock(g_mtx);
                g_inFlight = false;
                g_lastFetchMs = NowMs();
                if (!obj) SetErrorLocked(error ? "网络请求失败（检查网络/面板）" : "响应解析失败");
            });
        }];
    [task resume];
}

void DoSend(const std::string &text) {
    std::string license, hwid;
    if (!ChatCredentials(license, hwid)) {
        std::lock_guard<std::mutex> lock(g_mtx);
        SetErrorLocked("请先在「主页」登录卡密");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_snap.sending) return;
        g_snap.sending = true;
        g_snap.error.clear();
    }
    NSURL *url = [NSURL URLWithString:[NSString stringWithFormat:@"%@/api/chat/send", ChatBaseUrl()]];
    if (!url) {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_snap.sending = false;
        SetErrorLocked("URL 无效");
        return;
    }
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.HTTPMethod = @"POST";
    [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    NSDictionary *payload = @{ @"license": NsString(license),
                               @"hwid": NsString(hwid),
                               @"content": NsString(text) };
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:payload options:0 error:nil];
    NSURLSessionDataTask *task = [ChatSession() dataTaskWithRequest:req
        completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSDictionary *obj = nil;
            if (!error && data.length > 0) {
                obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            }
            bool ok = false;
            long long id = 0;
            std::string err;
            if ([obj isKindOfClass:[NSDictionary class]]) {
                ok = [obj[@"ok"] boolValue];
                id = [obj[@"id"] longLongValue];
                NSString *e = obj[@"error"];
                if (e) err = StdString(e);
            } else {
                err = error ? "网络请求失败（检查网络/面板）" : "响应解析失败";
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                {
                    std::lock_guard<std::mutex> lock(g_mtx);
                    g_snap.sending = false;
                    if (ok) {
                        g_snap.error.clear();
                        if (id > g_lastId) g_lastId = id;
                    } else {
                        SetErrorLocked(err.empty() ? "发送失败" : err);
                    }
                }
                if (ok) DoListFetch(false);   // 发成功后立刻拉一条增量
            });
        }];
    [task resume];
}

}  // namespace

// ---------------------------------------------------------------- 对外接口
void Tick() {
    if (NowMs() - g_lastFetchMs < 5000) return;
    std::string license, hwid;
    const bool available = ChatCredentials(license, hwid) &&
                           (Weiyan::IsVerified() || SesiAktif());
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_snap.available = available;
        if (!available) {
            if (g_snap.error.empty()) g_snap.error = "请先在「主页」登录卡密";
            return;
        }
    }
    if (!available) return;
    DoListFetch(false);
}

Snapshot GetSnapshot() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_snap;
}

void RefreshNow(bool forceFullReload) {
    DoListFetch(forceFullReload);
}

void Send(const std::string &text) {
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
        trimmed.pop_back();
    if (trimmed.empty()) return;
    if (trimmed.size() > 200) trimmed.resize(200);   // 服务端限制单条 ≤200，客户端先截断
    DoSend(trimmed);
}

void ShowInputDialog() {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIWindow *window = nil;
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (![scene isKindOfClass:[UIWindowScene class]]) continue;
            for (UIWindow *w in ((UIWindowScene *) scene).windows) {
                if (w.isKeyWindow) { window = w; break; }
            }
            if (window) break;
        }
        if (!window) window = UIApplication.sharedApplication.keyWindow;
        UIViewController *vc = window.rootViewController;
        while (vc.presentedViewController) vc = vc.presentedViewController;
        if (!vc) return;
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"发送聊天消息"
                                                                       message:nil
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField *tf) {
            tf.placeholder = @"最多 200 字";
            tf.clearButtonMode = UITextFieldViewModeWhileEditing;
        }];
        [alert addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];
        UIAlertController *strongAlert = alert;
        [alert addAction:[UIAlertAction actionWithTitle:@"发送" style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction *action) {
            UITextField *tf = strongAlert.textFields.firstObject;
            Send(StdString(tf.text ?: @""));
        }]];
        [vc presentViewController:alert animated:YES completion:nil];
    });
}

void Reset() {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_messages.clear();
    g_lastId = 0;
    g_lastFetchMs = 0;
    g_inFlight = false;
    g_snap = Snapshot{};
}

}  // namespace SMChatRoom
