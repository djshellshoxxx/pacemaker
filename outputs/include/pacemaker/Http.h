// Small HTTP/1.1 server with Server-Sent Events (RS-07). POSIX sockets, no dependencies.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

struct HttpRequest { std::string method, path, query, body; std::map<std::string, std::string> headers; };
struct HttpResponse {
    HttpResponse(int s = 200, std::string ct = "application/json", std::string b = {},
                 std::map<std::string, std::string> h = {}, bool stream = false)
        : status(s), contentType(std::move(ct)), body(std::move(b)), headers(std::move(h)), sse(stream) {}
    int status;
    std::string contentType;
    std::string body;
    std::map<std::string, std::string> headers;
    bool sse;                  // handler asks the server to keep the connection as an event stream
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    static constexpr size_t kMaxBody = 256 * 1024;

    ~HttpServer() { stop(); }
    // port 0 picks a free port. Returns false and fills error on failure.
    bool start(const std::string& bindAddr, int port, Handler h, std::string* error);
    void stop();
    int port() const { return port_; }
    // Sends one event to every stream client; drops clients that are gone or too slow.
    void broadcast(const std::string& event, const std::string& data);
    size_t streamClients();

private:
    void acceptLoop();
    void serve(int fd);
    int listenFd_ = -1, port_ = 0;
    Handler handler_;
    std::atomic<bool> running_ { false };
    std::thread acceptThread_;
    std::atomic<int> active_ { 0 };
    std::mutex clientsMutex_;
    std::vector<int> clients_;
};

} // namespace pacemaker
