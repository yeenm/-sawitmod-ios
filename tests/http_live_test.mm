#include "App/HttpClient.h"
#include <iostream>
#if !defined(SM_HTTP_BASELINE)
namespace SMHttp { Response TestGetViaNode(const char*,const char*); }
#endif
int main() {
#if defined(SM_HTTP_BASELINE)
    auto r=SMHttp::Get("https://api.yslf.work/api/?id=notice",10);
    std::cout << "BASELINE_NSURLSESSION status=" << r.code << " transport=" << r.transportOk << " body-bytes=" << r.body.size() << '\n';
    return r.transportOk && r.code==200 && !r.body.empty() ? 0 : 1;
#else
    for(const char* ip : {"216.167.14.42","156.239.4.155"}) {
        auto r=SMHttp::TestGetViaNode(ip,"api.yslf.work");
        std::cout << "MODIFIED_PINNED_TLS node=" << ip << " status=" << r.code << " transport=" << r.transportOk << " body-bytes=" << r.body.size() << '\n';
        if(!r.transportOk || r.code!=200 || r.body.empty()) return 1;
    }
    auto wrong=SMHttp::TestGetViaNode("216.167.14.42","invalid-peer.example");
    std::cout << "WRONG_TLS_HOST_REJECTED=" << !wrong.transportOk << '\n';
    if(wrong.transportOk) return 1;
    auto normal=SMHttp::Get("https://api.yslf.work/api/?id=notice",8);
    std::cout << "MODIFIED_PUBLIC_GET status=" << normal.code << " transport=" << normal.transportOk << '\n';
    return normal.transportOk && normal.code==200 ? 0 : 1;
#endif
}
