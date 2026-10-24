// Section change suggestions and dual-instance redundancy arbitration (DIFFERENTIATION.md items 10, 11).
#pragma once
#include <cmath>
#include <cstdint>
#include <deque>

namespace pacemaker {

// Feeds one mean-square energy value per bar. Returns +1 when the band gets clearly louder for two bars in a
// row (verse -> chorus), -1 when clearly quieter (breakdown), else 0. Suggestions only; nothing fires automatically.
class SectionDetector {
public:
    void reset() { hist_.clear(); up_ = down_ = 0; cooldown_ = 0; }
    int onBar(double meanSquare)
    {
        const double db = 10.0 * std::log10(meanSquare + 1e-12);
        int out = 0;
        if (hist_.size() >= 4) {
            double base = 0; for (double v : hist_) base += v;
            base /= (double) hist_.size();
            const double d = db - base;
            up_ = d > 4.0 ? up_ + 1 : 0;
            down_ = d < -5.0 ? down_ + 1 : 0;
            if (cooldown_ > 0) --cooldown_;
            else if (up_ >= 2) { out = 1; cooldown_ = 8; up_ = down_ = 0; }
            else if (down_ >= 2) { out = -1; cooldown_ = 8; up_ = down_ = 0; }
        }
        hist_.push_back(db);
        if (hist_.size() > 4) hist_.pop_front();
        return out;
    }
private:
    std::deque<double> hist_;
    int up_ = 0, down_ = 0, cooldown_ = 0;
};

// Two machines run Pacemaker on the same band; exactly one publishes. Heartbeats carry score and active flag.
class RedundancyArbiter {
public:
    explicit RedundancyArbiter(uint32_t myId = 1) : id_(myId) {}
    void setId(uint32_t id) { id_ = id; }

    static float scoreOf(float confidence, int state, bool follow)
    {
        if (!follow) return 0.0f;
        const bool locked = state == 3 || state == 5;     // Locked, Chase
        return locked ? 0.5f + 0.5f * confidence : (state == 4 ? 0.25f * confidence : 0.0f);
    }
    void onPeer(uint32_t id, float score, bool peerActive, int64_t nowUs)
    {
        peerId_ = id; peerScore_ = score; peerActive_ = peerActive; seenUs_ = nowUs; valid_ = true;
    }

    // True when this instance should publish. The current publisher gets a 0.1 bonus (hysteresis); both sides
    // evaluate the same symmetric rule, so exactly one wins. A silent peer (> timeout) hands over immediately.
    bool decide(float myScore, int64_t nowUs, int64_t timeoutUs = 1000000)
    {
        if (!peerAlive(nowUs, timeoutUs)) { active_ = true; return true; }
        const float mine = myScore + (active_ ? 0.1f : 0.0f);
        const float theirs = peerScore_ + (peerActive_ ? 0.1f : 0.0f);
        if (std::fabs(mine - theirs) < 0.001f) active_ = id_ < peerId_;
        else active_ = mine > theirs;
        return active_;
    }
    bool active() const { return active_; }
    bool peerAlive(int64_t nowUs, int64_t timeoutUs = 1000000) const { return valid_ && nowUs - seenUs_ <= timeoutUs; }
    uint32_t id() const { return id_; }
private:
    uint32_t id_, peerId_ = 0;
    float peerScore_ = 0;
    bool peerActive_ = false, valid_ = false, active_ = true;
    int64_t seenUs_ = 0;
};

} // namespace pacemaker
