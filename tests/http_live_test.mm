#include "App/HttpClient.h"
#include <iostream>
#if !defined(SM_HTTP_BASELINE)
namespace SMHttp { Response TestGetViaNode(const char*,const char*); }
#endif
int main() {
#if defined(SM_HTTP_BASELINE)
    auto r=SMHttp::Get("https://lldf.bid:2087/api/?id=notice",10);
    std::cout << "BASELINE_NSURLSESSION status=" << r.code << " transport=" << r.transportOk << " body-bytes=" << r.body.size() << '\n';
    return r.transportOk && r.code==200 && !r.body.empty() ? 0 : 1;
#else
    auto normal=SMHttp::Get("https://lldf.bid:2087/api/?id=notice",8);
    std::cout << "MODIFIED_PUBLIC_GET status=" << normal.code << " transport=" << normal.transportOk << '\n';
    return normal.transportOk && normal.code==200 ? 0 : 1;
#endif
}
