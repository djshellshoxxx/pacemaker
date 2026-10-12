// Single-writer, many-reader seqlock for trivially copyable T (ES-04 section 3).
// The payload is stored as relaxed atomic words so concurrent reads are data-race free;
// readers retry while the writer is mid-update. Lock-free, allocation-free, RT-safe on both sides.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pacemaker { namespace core {

template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable<T>::value, "Seqlock<T> needs a trivially copyable T");
    static constexpr size_t kWords = (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(uint64_t);

public:
    Seqlock() { store(T {}); }
    explicit Seqlock(const T& v) { store(v); }

    // Writer (one thread only).
    void store(const T& v) noexcept
    {
        uint64_t buf[kWords] = {};
        std::memcpy(buf, &v, sizeof(T));
        const uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);          // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < kWords; ++i) data_[i].store(buf[i], std::memory_order_relaxed);
        seq_.store(s + 2, std::memory_order_release);
    }

    // Single attempt; false when a write overlapped.
    bool tryLoad(T& out) const noexcept
    {
        uint64_t buf[kWords];
        const uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u) return false;
        for (size_t i = 0; i < kWords; ++i) buf[i] = data_[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) != s1) return false;
        std::memcpy(&out, buf, sizeof(T));
        return true;
    }

    // Spins until a consistent copy is read (writer updates are short).
    T load() const noexcept
    {
        T out;
        while (!tryLoad(out)) {}
        return out;
    }

    uint32_t version() const noexcept { return seq_.load(std::memory_order_acquire); }

private:
    std::atomic<uint32_t> seq_ { 0 };
    std::atomic<uint64_t> data_[kWords];
};

}} // namespace pacemaker::core
