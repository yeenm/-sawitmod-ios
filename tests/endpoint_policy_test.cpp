#include "App/EndpointPolicy.h"
#include <cstdlib>
#include <iostream>
#include <vector>

static int passed;
static void Check(bool ok, const char* name) {
    if (!ok) { std::cerr << "FAIL " << name << '\n'; std::exit(1); }
    ++passed; std::cout << "PASS " << name << '\n';
}
static SMHttp::Response Reply(long code, const char* body) {
    SMHttp::Response out; out.code=code; out.body=body; out.transportOk=true; return out;
}
int main() {
    using namespace SMHttp::Detail;
    Check(std::string(NodeAddress(0))=="216.167.14.42" && std::string(NodeAddress(1))=="156.239.4.155", "primary and backup address order");
    Check(IsPanelHost("API.YSLF.WORK") && IsPanelHost("yslf.work") && !IsPanelHost("other.example"), "allowlisted panel hosts only");
    FailoverState state;
    std::vector<int> calls;
    auto exchange = [&](int node) { calls.push_back(node); return node==0 ? SMHttp::Response{} : Reply(200,"ok"); };
    auto r=state.Run(exchange,1000);
    Check(r.body=="ok" && calls==std::vector<int>({0,1}),"network failure switches to backup");
    calls.clear(); state.Run(exchange,2000);
    Check(calls==std::vector<int>({1}),"working backup is preferred for 30 seconds");
    calls.clear(); state.Run(exchange,30000);
    Check(calls==std::vector<int>({1}),"backup stays available near the end of cooldown");
    calls.clear(); state.Run(exchange,31001);
    Check(calls==std::vector<int>({0,1}),"backup traffic does not extend primary retry cooldown");
    for (long code : {500,502,503,504,599}) {
        FailoverState s; calls.clear();
        r=s.Run([&](int n){calls.push_back(n);return Reply(n==0?code:200,n==0?"error":"ok");},1000);
        Check(r.body=="ok" && calls==std::vector<int>({0,1}),"5xx retries backup");
    }
    for (long code : {200,400,401,403,409,429}) {
        FailoverState s; calls.clear();
        r=s.Run([&](int n){calls.push_back(n);return Reply(code,"denied");},1000);
        Check(r.body=="denied" && calls==std::vector<int>({0}),"valid denial never retries backup");
    }
    { FailoverState s; calls.clear(); r=s.Run([&](int n){calls.push_back(n);return Reply(302,"redirect");},1000);
      Check(!r.transportOk && calls.size()==1,"redirect is rejected without another request"); }
    { FailoverState s; int count=0; r=s.Run([&](int){++count;return SMHttp::Response{};},1000);
      Check(!r.transportOk && count==2,"both failed nodes remain a network error"); }
    { FailoverState s; std::vector<std::string> payloads;
      const std::string payload="{\"nonce\":\"same\",\"card\":\"fixture\",\"message\":\"测试\"}";
      s.Run([&](int n){payloads.push_back(payload);return Reply(n?200:503,"ok");},1000);
      Check(payloads.size()==2 && payloads[0]==payloads[1],"POST body and nonce survive failover unchanged"); }
    SMHttp::Response parsed;
    auto parse=[&](const std::string& wire,bool eof=false){parsed={};return ParseReply(wire,eof,parsed);};
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok")==ParseResult::Complete && parsed.body=="ok","content-length response");
    Check(parse("HTTP/1.1 403 Forbidden\r\nContent-Length: 6\r\n\r\ndenied")==ParseResult::Complete && parsed.code==403,"complete 403 body is retained");
    Check(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;x=y\r\nok\r\n0\r\nX-End: yes\r\n\r\n")==ParseResult::Complete && parsed.body=="ok","chunk extensions and trailers");
    Check(parse("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok")==ParseResult::Complete,"interim 100 response");
    Check(parse("HTTP/1.0 200 OK\r\n\r\nok",true)==ParseResult::Complete && parsed.body=="ok","EOF-delimited response");
    Check(parse("HTTP/1.1 204 No Content\r\n\r\n")==ParseResult::Complete && parsed.body.empty(),"204 without a body");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nok")==ParseResult::More,"partial content waits for remaining bytes");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nok",true)==ParseResult::Invalid,"truncated response is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\nok",true)==ParseResult::Invalid,"ambiguous framing is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",true)==ParseResult::Invalid,"negative length is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999999\r\n\r\n",true)==ParseResult::Invalid,"overflow length is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 8388609\r\n\r\n",true)==ParseResult::Invalid,"body limit is enforced");
    Check(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nokX\n0\r\n\r\n",true)==ParseResult::Invalid,"broken chunk ending is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\nok",true)==ParseResult::Invalid,"unexpected content encoding is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nok",true)==ParseResult::Invalid,"duplicate framing header is rejected");
    Check(parse("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\nok",true)==ParseResult::Complete && parsed.body=="ok","duplicate ordinary headers remain compatible");
    std::cout << "IOS_ENDPOINT_TESTS_PASS=" << passed << '\n';
}
