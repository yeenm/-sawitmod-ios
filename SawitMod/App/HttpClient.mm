// ============================================================================
//  HttpClient.mm — 同步 HTTP（NSURLSession）
// ============================================================================

#include "App/HttpClient.h"

#import <Foundation/Foundation.h>

#include <dispatch/dispatch.h>

namespace {

SMHttp::Response Perform(NSString *method, const std::string &url, const std::string &body,
                         const std::string &contentType, int timeoutSec) {
    SMHttp::Response out;
    NSString *urlStr = [NSString stringWithUTF8String:url.c_str()];
    if (!urlStr) return out;
    NSURL *nsurl = [NSURL URLWithString:urlStr];
    if (!nsurl) return out;

    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:nsurl];
    req.HTTPMethod = method;
    req.timeoutInterval = (NSTimeInterval)(timeoutSec > 0 ? timeoutSec : 8);
    if (!body.empty()) {
        req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        [req setValue:[NSString stringWithUTF8String:contentType.c_str()]
            forHTTPHeaderField:@"Content-Type"];
    }
    // 与 Android 版一致的 UA（服务端据此区分平台/版本；这里改成 iOS 后缀）。
    NSString *version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString *ua = [NSString stringWithFormat:@"SawitMod-iOS/%@", version ?: @"v1.1"];
    [req setValue:ua forHTTPHeaderField:@"User-Agent"];
    [req setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];

    NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    cfg.timeoutIntervalForRequest = req.timeoutInterval;
    cfg.timeoutIntervalForResource = req.timeoutInterval + 4;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];

    __block SMHttp::Response result;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    NSURLSessionDataTask *task = [session dataTaskWithRequest:req
                                            completionHandler:^(NSData *data,
                                                                NSURLResponse *response,
                                                                NSError *error) {
        if (!error && response) {
            result.transportOk = true;
            if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                result.code = (long)[(NSHTTPURLResponse *)response statusCode];
            }
            if (data.length > 0) {
                result.body.assign((const char *)data.bytes, (size_t)data.length);
            }
        }
        dispatch_semaphore_signal(sem);
    }];
    [task resume];

    const int64_t waitMs = (int64_t)(req.timeoutInterval + 4) * 1000;
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, waitMs * NSEC_PER_MSEC)) != 0) {
        [task cancel];
        result.transportOk = false;
        result.body.clear();
    }
    [session finishTasksAndInvalidate];
    return result;
}

}  // namespace

namespace SMHttp {

Response Post(const std::string &url, const std::string &body, const std::string &contentType,
              int timeoutSec) {
    return Perform(@"POST", url, body, contentType, timeoutSec);
}

Response Get(const std::string &url, int timeoutSec) {
    return Perform(@"GET", url, std::string(), "application/json", timeoutSec);
}

std::string PostJson(const std::string &url, const std::string &json, int timeoutSec) {
    Response r = Post(url, json, "application/json", timeoutSec);
    return r.transportOk ? r.body : std::string();
}

}  // namespace SMHttp
