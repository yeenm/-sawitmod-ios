#pragma once
#include "HttpClient.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace SMHttp { namespace Detail {

inline const char* NodeAddress(int node) {
    return node == 0 ? "216.167.14.42" : "156.239.4.155";
}
inline std::string Lower(std::string value) {
    for (char& c : value) c = (char)std::tolower((unsigned char)c);
    return value;
}
inline bool IsPanelHost(const std::string& host) {
    std::string value = Lower(host);
    return value == "api.yslf.work" || value == "yslf.work";
}
inline int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class FailoverState {
    std::atomic<int> preferred_{0};
    std::atomic<int64_t> until_{0};
public:
    template <typename Exchange>
    Response Run(Exchange exchange, int64_t now = NowMs()) {
        const int start = now < until_.load() ? preferred_.load() : 0;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const int node = (start + attempt) % 2;
            Response reply = exchange(node);
            if (!reply.transportOk || (reply.code >= 500 && reply.code <= 599)) continue;
            // Redirects never carry credentials to another origin.
            if (reply.code >= 300 && reply.code < 400) return Response{};
            const int previous = preferred_.exchange(node);
            if (node == 0) until_.store(0);
            else if (previous != node || now >= until_.load()) until_.store(now + 30000);
            return reply; // Includes signed 401/403 and protocol-level denials.
        }
        return Response{};
    }
};

enum class ParseResult { More, Complete, Invalid };
inline std::string Trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
}
inline bool Number(const std::string& s, unsigned base, size_t& out) {
    if (s.empty()) return false;
    out = 0;
    for (unsigned char c : s) {
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 :
            c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
        if (digit >= base || out > (std::numeric_limits<size_t>::max() - digit) / base) return false;
        out = out * base + digit;
    }
    return true;
}

// Parse complete HTTP/1.x frames without accepting a truncated denial/signed body.
// Incomplete chunked frames are scanned without copying their body on each read.
inline ParseResult ParseReply(const std::string& wire, bool eof, Response& out) {
    constexpr size_t maxBody = 8 * 1024 * 1024, maxHeaders = 65536;
    if (wire.size() > 16 * 1024 * 1024) return ParseResult::Invalid;
    size_t begin = 0, bodyBegin = 0;
    long status = 0;
    std::map<std::string, std::string> headers;
    for (int interim = 0; ; ++interim) {
        if (interim >= 5) return ParseResult::Invalid;
        size_t end = wire.find("\r\n\r\n", begin);
        if (end == std::string::npos) return wire.size() - begin > maxHeaders || eof
            ? ParseResult::Invalid : ParseResult::More;
        if (end - begin > maxHeaders) return ParseResult::Invalid;
        size_t firstEnd = wire.find("\r\n", begin);
        std::string first = wire.substr(begin, firstEnd - begin);
        if (first.size() < 12 || (first.compare(0, 9, "HTTP/1.1 ") && first.compare(0, 9, "HTTP/1.0 "))
            || !std::isdigit((unsigned char)first[9]) || !std::isdigit((unsigned char)first[10])
            || !std::isdigit((unsigned char)first[11]) || (first.size() > 12 && first[12] != ' '))
            return ParseResult::Invalid;
        status = (first[9] - '0') * 100 + (first[10] - '0') * 10 + first[11] - '0';
        if (status < 100 || status > 599) return ParseResult::Invalid;
        headers.clear();
        for (size_t line = firstEnd + 2; line < end; ) {
            size_t next = wire.find("\r\n", line), colon = wire.find(':', line);
            if (colon == std::string::npos || colon <= line || colon >= next) return ParseResult::Invalid;
            std::string key = Lower(wire.substr(line, colon - line));
            if (headers.count(key) && (key == "content-length" || key == "transfer-encoding"
                || key == "content-encoding")) return ParseResult::Invalid;
            headers[key] = Trim(wire.substr(colon + 1, next - colon - 1));
            line = next + 2;
        }
        bodyBegin = end + 4;
        if (status >= 200) break;
        if (status == 101) return ParseResult::Invalid;
        begin = bodyBegin;
    }
    std::string body;
    if (headers.count("content-encoding") && Lower(headers["content-encoding"]) != "identity")
        return ParseResult::Invalid;
    if (status != 204 && status != 304) {
        if (headers.count("transfer-encoding")) {
            if (Lower(headers["transfer-encoding"]) != "chunked" || headers.count("content-length"))
                return ParseResult::Invalid;
            std::vector<std::pair<size_t, size_t>> chunks;
            size_t pos = bodyBegin, total = 0;
            for (;;) {
                size_t end = wire.find("\r\n", pos);
                if (end == std::string::npos) return eof ? ParseResult::Invalid : ParseResult::More;
                if (end - pos > 16384) return ParseResult::Invalid;
                std::string sizeLine = wire.substr(pos, end - pos);
                size_t semi = sizeLine.find(';');
                size_t count;
                if (!Number(Trim(sizeLine.substr(0, semi)), 16, count) || count > maxBody - total)
                    return ParseResult::Invalid;
                pos = end + 2;
                if (count == 0) {
                    size_t trailerBegin = pos;
                    for (;;) {
                        end = wire.find("\r\n", pos);
                        if (end == std::string::npos) return eof || wire.size() - trailerBegin > maxHeaders
                            ? ParseResult::Invalid : ParseResult::More;
                        if (end - trailerBegin > maxHeaders) return ParseResult::Invalid;
                        if (end == pos) break;
                        size_t colon = wire.find(':', pos);
                        if (colon == std::string::npos || colon <= pos || colon >= end) return ParseResult::Invalid;
                        pos = end + 2;
                    }
                    body.reserve(total);
                    for (auto chunk : chunks) body.append(wire, chunk.first, chunk.second);
                    break;
                }
                if (wire.size() - pos < count + 2) return eof ? ParseResult::Invalid : ParseResult::More;
                if (wire.compare(pos + count, 2, "\r\n")) return ParseResult::Invalid;
                chunks.push_back({pos, count});
                total += count;
                pos += count + 2;
            }
        } else if (headers.count("content-length")) {
            size_t length;
            if (!Number(headers["content-length"], 10, length) || length > maxBody) return ParseResult::Invalid;
            if (wire.size() - bodyBegin < length) return eof ? ParseResult::Invalid : ParseResult::More;
            body = wire.substr(bodyBegin, length);
        } else {
            if (wire.size() - bodyBegin > maxBody) return ParseResult::Invalid;
            if (!eof) return ParseResult::More;
            body = wire.substr(bodyBegin);
        }
    }
    out.code = status;
    out.body = std::move(body);
    out.transportOk = true;
    return ParseResult::Complete;
}

}} // namespace SMHttp::Detail
