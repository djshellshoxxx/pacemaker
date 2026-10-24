// Tiny thread-safe logger with size-based rotation (RS-01: "logs to a rotating file"). No JUCE.
#pragma once
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace pmhl {

class Log {
public:
    static Log& get() { static Log l; return l; }

    // Empty path = stderr only. Rotation: file -> file.1 -> file.2 ... (keep files).
    void open(const std::string& path, long maxBytes, int keep, bool alsoStderr)
    {
        std::lock_guard<std::mutex> l(m_);
        path_ = path; maxBytes_ = maxBytes > 0 ? maxBytes : 1 << 20; keep_ = keep < 1 ? 1 : keep; stderr_ = alsoStderr || path.empty();
        if (f_) { std::fclose(f_); f_ = nullptr; }
        if (!path_.empty()) {
            mkdirs(parent(path_));
            f_ = std::fopen(path_.c_str(), "a");
            if (!f_) { stderr_ = true; std::fprintf(stderr, "pacemaker: cannot open log file %s, logging to stderr\n", path_.c_str()); }
        }
    }
    void close() { std::lock_guard<std::mutex> l(m_); if (f_) { std::fclose(f_); f_ = nullptr; } }

#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    void write(const char* level, const char* fmt, ...)
    {
        char msg[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
        char ts[40];
        const std::time_t t = std::time(nullptr); std::tm tmv {};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
        std::lock_guard<std::mutex> l(m_);
        if (f_) {
            std::fprintf(f_, "%s %-5s %s\n", ts, level, msg);
            std::fflush(f_);
            if (std::ftell(f_) > maxBytes_) rotate();
        }
        if (stderr_) std::fprintf(stderr, "%s %-5s %s\n", ts, level, msg);
    }

private:
    static std::string parent(const std::string& p) { const auto s = p.find_last_of('/'); return s == std::string::npos ? std::string() : p.substr(0, s); }
    static void mkdirs(const std::string& dir)
    {
        if (dir.empty()) return;
        for (size_t i = 1; i <= dir.size(); ++i)
            if (i == dir.size() || dir[i] == '/') ::mkdir(dir.substr(0, i).c_str(), 0755);
    }
    void rotate()
    {
        std::fclose(f_); f_ = nullptr;
        for (int i = keep_ - 1; i >= 1; --i) {
            const std::string from = i == 1 ? path_ : path_ + "." + std::to_string(i - 1);
            std::rename(from.c_str(), (path_ + "." + std::to_string(i)).c_str());
        }
        f_ = std::fopen(path_.c_str(), "a");
    }
    std::mutex m_;
    std::FILE* f_ = nullptr;
    std::string path_;
    long maxBytes_ = 1 << 20;
    int keep_ = 3;
    bool stderr_ = true;
};

} // namespace pmhl

#define PM_LOGI(...) ::pmhl::Log::get().write("INFO", __VA_ARGS__)
#define PM_LOGW(...) ::pmhl::Log::get().write("WARN", __VA_ARGS__)
#define PM_LOGE(...) ::pmhl::Log::get().write("ERROR", __VA_ARGS__)
