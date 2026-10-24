// Per-output status shared with the UI through atomics (ES-02 section 8).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace pacemaker {

struct OutputStatus {
    std::atomic<bool> enabled { false };
    std::atomic<bool> connected { false };
    std::atomic<int> measuredJitterUs { 0 };
    std::atomic<int> peers { 0 };          // Link peers
    std::atomic<int> errors { 0 };
    std::atomic<uint64_t> sent { 0 };

    void setError(const std::string& s) { std::lock_guard<std::mutex> l(m_); err_ = s; errors.fetch_add(1); }
    void clearError() { std::lock_guard<std::mutex> l(m_); err_.clear(); }
    std::string lastError() const { std::lock_guard<std::mutex> l(m_); return err_; }
    void setDetail(const std::string& s) { std::lock_guard<std::mutex> l(m_); detail_ = s; }
    std::string detail() const { std::lock_guard<std::mutex> l(m_); return detail_; }
private:
    mutable std::mutex m_;
    std::string err_, detail_;
};

} // namespace pacemaker
