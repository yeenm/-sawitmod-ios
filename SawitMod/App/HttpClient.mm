// ============================================================================
//  HttpClient.mm — 同步 HTTP（NSURLSession）
// ============================================================================

#include "App/HttpClient.h"
#include "App/EndpointPolicy.h"

#import <Foundation/Foundation.h>
#import <CFNetwork/CFNetwork.h>
#import <Security/Security.h>

#include <dispatch/dispatch.h>

namespace {

SMHttp::Response PerformSession(NSString *method, const std::string &url, const std::string &body,
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
        [session invalidateAndCancel];
        return out;
    }
    [session finishTasksAndInvalidate];
    return result;
}


// A TCP stream targets the chosen IP. The SSL peer name remains the URL hostname,
// which supplies SNI and certificate name/chain validation to the system TLS stack.
SMHttp::Response PerformPinned(NSString *method, NSURL *url, const std::string& body,
                              const std::string& contentType, int timeoutSec, const char* address) {
    SMHttp::Response out;
    CFReadStreamRef input = nullptr;
    CFWriteStreamRef output = nullptr;
    NSString *node = [NSString stringWithUTF8String:address];
    CFStreamCreatePairWithSocketToHost(kCFAllocatorDefault, (__bridge CFStringRef)node,
        443, &input, &output);
    if (!input || !output) {
        if (input) CFRelease(input);
        if (output) CFRelease(output);
        return out;
    }
    NSDictionary *tls = @{
        (__bridge NSString *)kCFStreamSSLLevel: (__bridge NSString *)kCFStreamSocketSecurityLevelNegotiatedSSL,
        (__bridge NSString *)kCFStreamSSLPeerName: url.host,
        (__bridge NSString *)kCFStreamSSLValidatesCertificateChain: @YES
    };
    bool configured = CFReadStreamSetProperty(input, kCFStreamPropertySSLSettings, (__bridge CFTypeRef)tls)
        && CFWriteStreamSetProperty(output, kCFStreamPropertySSLSettings, (__bridge CFTypeRef)tls);
    CFRunLoopRef loop = CFRunLoopGetCurrent();
    CFStringRef mode = CFSTR("SMHttpPinnedEndpoint");
    CFReadStreamScheduleWithRunLoop(input, loop, mode);
    CFWriteStreamScheduleWithRunLoop(output, loop, mode);
    const int64_t deadline = SMHttp::Detail::NowMs() + std::min(timeoutSec > 0 ? timeoutSec : 8, 5) * 1000;
    if (configured && CFReadStreamOpen(input) && CFWriteStreamOpen(output)) {
        NSURLComponents *parts = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
        NSString *target = parts.percentEncodedPath.length ? parts.percentEncodedPath : @"/";
        if (parts.percentEncodedQuery) target = [target stringByAppendingFormat:@"?%@", parts.percentEncodedQuery];
        NSString *version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        NSString *head = [NSString stringWithFormat:@"%@ %@ HTTP/1.1\r\nHost: %@\r\nUser-Agent: SawitMod-iOS/%@\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n",
            method, target, url.host, version ?: @"v1.1"];
        std::string request([head UTF8String]);
        if ([method isEqualToString:@"POST"]) {
            request += "Content-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        }
        request += "\r\n";
        if ([method isEqualToString:@"POST"]) request += body;
        size_t written = 0;
        std::string wire;
        UInt8 bytes[8192];
        while (SMHttp::Detail::NowMs() < deadline) {
            if (CFReadStreamGetStatus(input) == kCFStreamStatusError
                || CFWriteStreamGetStatus(output) == kCFStreamStatusError) break;
            if (written < request.size() && CFWriteStreamCanAcceptBytes(output)) {
                CFIndex count = CFWriteStreamWrite(output, (const UInt8*)request.data() + written,
                    (CFIndex)std::min<size_t>(request.size() - written, sizeof(bytes)));
                if (count <= 0) break;
                written += (size_t)count;
            }
            bool eof = CFReadStreamGetStatus(input) == kCFStreamStatusAtEnd;
            if (written == request.size() && (CFReadStreamHasBytesAvailable(input) || eof)) {
                CFIndex count = eof ? 0 : CFReadStreamRead(input, bytes, sizeof(bytes));
                if (count < 0) break;
                if (count > 0) wire.append((const char*)bytes, (size_t)count);
                auto parsed = SMHttp::Detail::ParseReply(wire, eof || count == 0, out);
                if (parsed != SMHttp::Detail::ParseResult::More) break;
            }
            CFRunLoopRunInMode(mode, 0.005, true);
        }
    }
    CFReadStreamClose(input);
    CFWriteStreamClose(output);
    CFReadStreamUnscheduleFromRunLoop(input, loop, mode);
    CFWriteStreamUnscheduleFromRunLoop(output, loop, mode);
    CFRelease(input);
    CFRelease(output);
    return out;
}

SMHttp::Response Perform(NSString *method, const std::string& url, const std::string& body,
                         const std::string& contentType, int timeoutSec) {
    @autoreleasepool {
        NSURL *parsed = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!parsed || !parsed.host) return {};
        // Resolve the API host through DNS/Cloudflare. Do not pin the origin IP.
        if (![parsed.scheme.lowercaseString isEqualToString:@"https"] || parsed.user || parsed.password
            || contentType.find_first_of("\r\n") != std::string::npos) return {};
        return PerformSession(method, url, body, contentType, timeoutSec);
    }
}

#if defined(SM_HTTP_TESTING)
SMHttp::Response TestPinnedGet(const char* address, const char* hostname) {
    @autoreleasepool {
        NSString *host = [NSString stringWithUTF8String:hostname];
        NSURL *url = [NSURL URLWithString:[NSString stringWithFormat:@"https://%@/api/?id=notice", host]];
        return PerformPinned(@"GET", url, {}, "application/json", 5, address);
    }
}
#endif

}  // namespace

namespace SMHttp {
#if defined(SM_HTTP_TESTING)
Response TestGetViaNode(const char* address, const char* host) { return TestPinnedGet(address, host); }
#endif

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
