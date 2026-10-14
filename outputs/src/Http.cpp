#include "pacemaker/Http.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pacemaker {

namespace {
const char* reason(int s)
{
    switch (s) { case 200: return "OK"; case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
                 case 413: return "Payload Too Large"; case 500: return "Internal Server Error"; default: return "OK"; }
}
bool sendAll(int fd, const char* p, size_t n)
{
    while (n) {
        pollfd pf { fd, POLLOUT, 0 };
        if (poll(&pf, 1, 2000) <= 0) return false;
        const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w; n -= (size_t) w;
    }
    return true;
}
std::string lowerStr(std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; }
}

bool HttpServer::start(const std::string& bindAddr, int port, Handler h, std::string* error)
{
    stop();
    handler_ = std::move(h);
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { if (error) *error = "socket failed"; return false; }
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a {};
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t) port);
    if (inet_pton(AF_INET, bindAddr.c_str(), &a.sin_addr) != 1 || ::bind(listenFd_, (sockaddr*) &a, sizeof a) != 0 || ::listen(listenFd_, 16) != 0) {
        if (error) *error = "cannot listen on " + bindAddr + ":" + std::to_string(port);
        ::close(listenFd_); listenFd_ = -1; return false;
    }
    socklen_t len = sizeof a;
    getsockname(listenFd_, (sockaddr*) &a, &len);
    port_ = ntohs(a.sin_port);
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop()
{
    if (!running_.exchange(false)) return;
    ::shutdown(listenFd_, SHUT_RDWR);
    if (acceptThread_.joinable()) acceptThread_.join();
    ::close(listenFd_); listenFd_ = -1;
    {
        std::lock_guard<std::mutex> l(clientsMutex_);
        for (int fd : clients_) ::close(fd);
        clients_.clear();
    }
    while (active_.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void HttpServer::acceptLoop()
{
    while (running_.load()) {
        pollfd pf { listenFd_, POLLIN, 0 };
        if (poll(&pf, 1, 200) <= 0) continue;
        const int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) continue;
        ++active_;
        std::thread([this, fd] { serve(fd); --active_; }).detach();
    }
}

void HttpServer::serve(int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    std::string buf;
    char tmp[4096];
    size_t headEnd = std::string::npos;
    auto respond = [&](const HttpResponse& r) {
        std::string h = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\nContent-Type: " + r.contentType +
                        "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n";
        for (const auto& kv : r.headers) h += kv.first + ": " + kv.second + "\r\n";
        h += "\r\n";
        sendAll(fd, h.data(), h.size()); sendAll(fd, r.body.data(), r.body.size());
    };
    // Read the header block.
    while (headEnd == std::string::npos) {
        pollfd pf { fd, POLLIN, 0 };
        if (poll(&pf, 1, 3000) <= 0) { ::close(fd); return; }
        const ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) { ::close(fd); return; }
        buf.append(tmp, (size_t) n);
        headEnd = buf.find("\r\n\r\n");
        if (headEnd == std::string::npos && buf.size() > 16384) { respond({ 413, "text/plain", "headers too large" }); ::close(fd); return; }
    }
    HttpRequest req;
    {
        const std::string head = buf.substr(0, headEnd);
        size_t le = head.find("\r\n");
        const std::string line = head.substr(0, le);
        const size_t s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
        if (s1 == std::string::npos || s2 == std::string::npos) { respond({ 400, "text/plain", "bad request" }); ::close(fd); return; }
        req.method = line.substr(0, s1);
        std::string target = line.substr(s1 + 1, s2 - s1 - 1);
        const size_t q = target.find('?');
        req.path = target.substr(0, q);
        if (q != std::string::npos) req.query = target.substr(q + 1);
        while (le != std::string::npos && le < head.size()) {
            const size_t ns = le + 2, ne = head.find("\r\n", ns);
            const std::string h = head.substr(ns, ne == std::string::npos ? std::string::npos : ne - ns);
            const size_t c = h.find(':');
            if (c != std::string::npos) {
                size_t v = c + 1; while (v < h.size() && h[v] == ' ') ++v;
                req.headers[lowerStr(h.substr(0, c))] = h.substr(v);
            }
            le = ne;
        }
    }
    size_t want = 0;
    if (req.headers.count("content-length")) {
        const long long cl = std::atoll(req.headers["content-length"].c_str());
        if (cl < 0 || (size_t) cl > kMaxBody) { respond({ 413, "application/json", "{\"ok\":false,\"error\":\"body too large\"}" }); ::close(fd); return; }
        want = (size_t) cl;
    }
    req.body = buf.substr(headEnd + 4);
    while (req.body.size() < want) {
        pollfd pf { fd, POLLIN, 0 };
        if (poll(&pf, 1, 3000) <= 0) { ::close(fd); return; }
        const ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) { ::close(fd); return; }
        req.body.append(tmp, (size_t) n);
        if (req.body.size() > kMaxBody) { respond({ 413, "application/json", "{\"ok\":false,\"error\":\"body too large\"}" }); ::close(fd); return; }
    }
    req.body.resize(std::min(req.body.size(), want));

    HttpResponse res;
    try { res = handler_(req); } catch (const std::exception& e) { res = { 500, "application/json", std::string("{\"ok\":false,\"error\":\"") + e.what() + "\"}" }; }
    if (res.sse) {
        const std::string h = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n"
                              "X-Accel-Buffering: no\r\n\r\nretry: 1500\n\n";
        if (!sendAll(fd, h.data(), h.size())) { ::close(fd); return; }
        std::lock_guard<std::mutex> l(clientsMutex_);
        if (running_.load() && clients_.size() < 32) clients_.push_back(fd); else ::close(fd);
        return;
    }
    respond(res);
    ::close(fd);
}

void HttpServer::broadcast(const std::string& event, const std::string& data)
{
    const std::string msg = "event: " + event + "\ndata: " + data + "\n\n";
    std::lock_guard<std::mutex> l(clientsMutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        const ssize_t w = ::send(*it, msg.data(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w != (ssize_t) msg.size()) { ::close(*it); it = clients_.erase(it); }   // gone, or too slow to keep up
        else ++it;
    }
}

size_t HttpServer::streamClients()
{
    std::lock_guard<std::mutex> l(clientsMutex_);
    return clients_.size();
}

} // namespace pacemaker
