// Fixed-capacity single-producer single-consumer ring (ES-04 section 3).
#pragma once
#include <atomic>
#include <cstddef>

namespace pacemaker {

template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    bool push(const T& v)
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
        buf_[w & (Capacity - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out)
    {
        const size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        out = buf_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    size_t size() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }
    size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    void clear() { T t; while (pop(t)) {} }
private:
    T buf_[Capacity] {};
    std::atomic<size_t> write_ { 0 }, read_ { 0 }, dropped_ { 0 };
};

} // namespace pacemaker
