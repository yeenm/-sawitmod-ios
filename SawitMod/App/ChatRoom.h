#pragma once
// ============================================================================
//  ChatRoom.h — 客户端聊天室（对接面板 yslf.work 的 /api/chat/*）
// ----------------------------------------------------------------------------
//  接口（详见 聊天室对接包/docs/API.md）：
//    GET  /api/chat/list?license=&hwid=&after=&limit=50   拉消息（增量）
//    POST /api/chat/send  {"license","hwid","content"}    发消息
//  鉴权：卡密 + 设备码 + 在线会话（必须先完成卡密登录），所以这里直接复用
//        Weiyan 的登录态，不需要额外配置服务器地址/密钥。
//  线程模型：Tick() 在渲染线程（主线程）调用，内部按 5 秒节流发起 NSURLSession；
//        完成回调在后台队列里写状态（加锁），GetSnapshot() 安全拷贝给 UI。
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace SMChatRoom {

struct Message {
    long long id = 0;
    std::string user;
    std::string text;
    std::string kind = "user";   // user | broadcast
    long long ts = 0;
};

struct Snapshot {
    bool available = false;      // 已登录（有卡密 + 设备码）
    bool loading = false;        // 正在拉列表
    bool sending = false;        // 正在发消息
    std::string error;           // 最近一次失败原因（成功拉取后清空）
    std::string me;              // 服务器生成的用户名（如 G2P2·月卡）
    int online = 0;              // 在线人数
    std::vector<Message> messages;   // 升序（服务端顺序，最新在末尾）
};

// 每帧调用（渲染线程）；内部按 5 秒节流轮询 + 登录态检查。
void Tick();

// 线程安全拷贝一份当前状态给 UI 渲染。
Snapshot GetSnapshot();

// 立即刷新。forceFullReload=true 时 after=0 全量重拉（清空本地列表）。
void RefreshNow(bool forceFullReload);

// 异步发送一条消息；发送中/失败原因通过 Snapshot 暴露。
void Send(const std::string& text);

// 弹系统键盘（UIAlertController + UITextField）输入并发送。
void ShowInputDialog();

// 登出/掉线时清空本地聊天状态。
void Reset();

}  // namespace SMChatRoom
