#include "Tracker.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

namespace {
constexpr double kWindowHalf = 0.30;   // expectancy window truncation, fraction of tau
constexpr double kWindowSigma = 0.15;
constexpr double kSubSigma = 0.10;
constexpr double kSubWeight = 0.30;
constexpr int kHoldAfterMissed = 2;
constexpr double kHoldTimeoutSec = 20.0;
constexpr double kConfDecaySec = 2.0;
constexpr int kChaseBeats = 4;
constexpr double kHistSec = 4.0;

inline double gauss(double e, double sigma) { return std::exp(-0.5 * (e / sigma) * (e / sigma)); }
inline bool finiteOr(double& v, double fallback) { if (!std::isfinite(v)) { v = fallback; return false; } return true; }
}

void Tracker::prepare(const EngineConfig& cfg, EventQueue* out, const TempoInduction* induction)
{
    cfg_ = cfg; sr_ = cfg.sampleRate; out_ = out; induction_ = induction;
    minTau_ = 60.0 * sr_ / std::max(cfg.tempoRange.maxBpm, 30.0);
    maxTau_ = 60.0 * sr_ / std::max(cfg.tempoRange.minBpm, 1.0);
    sigmaMeas_ = 0.012 * sr_;
    bar_.prepare(cfg.meter, cfg.pattern);
    reset();
}

void Tracker::reset()
{
    bar_.reset();
    state_ = EngineState::Idle; follow_ = true;
    phi_ = tau_ = pubPhi_ = pubTau_ = pubBpm_ = 0;
    hasPublished_ = relockPending_ = relockFlag_ = emitted_ = false;
    barNumber_ = 0; lastTick_ = 0; holdStart_ = 0; octaveSince_ = -1; octaveFlag_ = false;
    hit_ = false; bestE_ = 0; bestW_ = 0; evKick_ = evSnare_ = evStrong_ = false;
    hitMask_ = 0; innovN_ = innovPos_ = 0; missCount_ = lockHits_ = chaseBeats_ = sameSign_ = lastSign_ = 0;
    confidence_ = 0; lastBpm_ = 0; clicks_ = 0; prevOnset_ = -1; holdOutsideN_ = 0; lastTap_ = -1;
    histPos_ = histN_ = 0;
}

double Tracker::clampTau(double t) const { return std::clamp(t, minTau_, maxTau_); }

void Tracker::startBeatAt(double phi, EngineState s, int pos, int bar)
{
    phi_ = phi; state_ = s; emitted_ = false;
    hit_ = false; bestE_ = 0; bestW_ = 0; evKick_ = evSnare_ = evStrong_ = false;
    missCount_ = 0; lockHits_ = 0; chaseBeats_ = 0; sameSign_ = 0; hitMask_ = 0; innovN_ = 0;
    holdOutsideN_ = 0; relockPending_ = true; octaveSince_ = -1; octaveFlag_ = false;
    barNumber_ = bar;
    bar_.setPosition(pos, s == EngineState::CountIn);
}

void Tracker::goIdle()
{
    if (tau_ > 0) lastBpm_ = bpmOf(tau_);
    state_ = EngineState::Idle; clicks_ = 0; confidence_ = 0.0f; emitted_ = false;
}

// ---------------------------------------------------------------- onsets

void Tracker::onOnset(const FusedOnset& o)
{
    hist_[histPos_] = { o.sample, o.strength };
    histPos_ = (histPos_ + 1) % kHist; histN_ = std::min(histN_ + 1, kHist);
    tick(o.sample);

    if (state_ == EngineState::Idle) { countInDetect(o); prevOnset_ = o.sample; return; }

    const double e = (double) o.sample - phi_;
    const double half = kWindowHalf * tau_;
    const double sw = std::clamp((double) o.strength, 0.2, 1.0);

    if (std::fabs(e) <= half) {
        // Beat measurement. The best-scoring onset of the window is applied when the beat closes.
        const double score = gauss(e, kWindowSigma * tau_) * sw;
        if (state_ == EngineState::CountIn) {
            const bool quiet = prevOnset_ < 0 || (double) (o.sample - prevOnset_) >= 0.6 * tau_;
            if (quiet && clicks_ < 4 && !(o.tag & BandTag::KickLike)) {
                clickT_[clicks_++] = o.sample;
                double mean = 0; for (int i = 1; i < clicks_; ++i) mean += (double) (clickT_[i] - clickT_[i - 1]);
                mean /= (clicks_ - 1);
                double var = 0; for (int i = 1; i < clicks_; ++i) { const double d = (double) (clickT_[i] - clickT_[i - 1]) - mean; var += d * d; }
                const double cv = std::sqrt(var / (clicks_ - 1)) / mean;
                if (cv > 0.05) { goIdle(); prevOnset_ = o.sample; return; }
                tau_ = clampTau(mean); phi_ = (double) o.sample;   // clicks are applied directly
                hit_ = true; bestW_ = 0; bestE_ = 0;
                prevOnset_ = o.sample; return;
            }
            state_ = EngineState::Locked; relockPending_ = true;   // dense onsets: playing has started
        }
        hit_ = true;
        if (score > bestW_) { bestW_ = (float) score; bestE_ = e; bestSw_ = sw; }
        if (std::fabs(e) <= 0.2 * tau_) {
            if (o.tag & BandTag::KickLike) evKick_ = true;
            if (o.tag & BandTag::SnareLike) evSnare_ = true;
            if (o.strength > 0.8f) evStrong_ = true;
        }
        if (state_ == EngineState::Hold) { state_ = EngineState::Locked; missCount_ = 0; holdOutsideN_ = 0; }
    } else if (state_ == EngineState::Hold) {
        // Re-entry on a different phase: two consecutive onsets one period apart relock.
        if (holdOutsideN_ == 1 && std::fabs((double) (o.sample - holdOutside_[0]) - tau_) < 0.15 * tau_) {
            startBeatAt((double) o.sample, EngineState::Locked, bar_.position(), barNumber_);
            bar_.setPosition(bar_.position(), false);
        } else {
            holdOutside_[0] = o.sample; holdOutsideN_ = 1;
        }
    } else if (e < -half && std::fabs(e + 0.5 * tau_) <= 0.2 * tau_) {
        // Sub-beat (half-beat) measurement with reduced weight, applied immediately.
        if (state_ != EngineState::CountIn && follow_) {
            const double es = e + 0.5 * tau_;
            const double w = kSubWeight * gauss(es, kSubSigma * tau_) * sw;
            const double beta = state_ == EngineState::Chase ? cfg_.profile.chaseBeta : cfg_.profile.beta;
            phi_ += cfg_.profile.alpha * w * es;
            tau_ = clampTau(tau_ + beta * w * es);
        }
    } else if (state_ == EngineState::CountIn) {
        state_ = EngineState::Locked; relockPending_ = true;   // playing started off the grid
    }
    finiteOr(phi_, (double) o.sample); finiteOr(tau_, 0.5 * sr_);
    prevOnset_ = o.sample;
}

void Tracker::countInDetect(const FusedOnset& o)
{
    const bool tagOk = (o.tag & BandTag::KickLike) == 0;
    const double gap = prevOnset_ < 0 ? 1e12 : (double) (o.sample - prevOnset_);
    if (clicks_ == 1 && tagOk && gap >= minTau_ && gap <= maxTau_) {
        clicks_ = 2; clickT_[1] = o.sample;
        tau_ = gap;
        startBeatAt((double) o.sample + tau_, EngineState::CountIn, 3, 0);
        return;
    }
    if (gap >= 0.5 * sr_ && tagOk) { clicks_ = 1; clickT_[0] = o.sample; return; }
    clicks_ = 0;
}

// ---------------------------------------------------------------- clock

void Tracker::tick(int64_t now)
{
    if (now <= lastTick_) return;
    const double dt = (double) (now - lastTick_) / sr_;
    lastTick_ = now;

    if (state_ == EngineState::Idle) { maybeStartLocking(now); return; }

    if (state_ == EngineState::Hold) {
        confidence_ = std::max(0.05f, confidence_ * (float) std::exp(-dt / kConfDecaySec));
        if ((double) (now - holdStart_) > kHoldTimeoutSec * sr_) { goIdle(); return; }
    }

    // Octave ambiguity: induction peak near 2x or 0.5x the tracked tempo for more than 4 s.
    if (induction_ && induction_->numPeaks() > 0 && tau_ > 0) {
        const double ratio = induction_->bestBpm() / bpmOf(tau_);
        const bool near = std::fabs(ratio - 2.0) <= 0.08 || std::fabs(ratio - 0.5) <= 0.02;
        if (near) { if (octaveSince_ < 0) octaveSince_ = now; octaveFlag_ = (double) (now - octaveSince_) > 4.0 * sr_; }
        else { octaveSince_ = -1; octaveFlag_ = false; }
    }

    for (;;) {
        if (!emitted_ && (double) now >= phi_) emitBeat();
        if ((double) now >= phi_ + kWindowHalf * tau_) closeBeat(now); else break;
        if (state_ == EngineState::Idle) break;
    }
}

void Tracker::emitBeat()
{
    const double targetBpm = bpmOf(tau_);
    const double step = state_ == EngineState::Chase ? 2.0 : 0.5;
    if (relockPending_ || !hasPublished_ || state_ == EngineState::Locking) {
        // Rate limits apply once locked; while locking the published timeline follows freely.
        pubBpm_ = targetBpm; pubPhi_ = phi_;
        relockFlag_ = relockPending_ && hasPublished_;
        relockPending_ = false; hasPublished_ = true;
    } else {
        pubBpm_ = std::clamp(targetBpm, pubBpm_ - step, pubBpm_ + step);
        const double extrap = pubPhi_ + pubTau_, slew = cfg_.profile.maxSlewMs * 1e-3 * sr_;
        pubPhi_ = std::clamp(phi_, extrap - slew, extrap + slew);
        relockFlag_ = false;
    }
    pubTau_ = 60.0 * sr_ / pubBpm_;
    emitted_ = true;

    if (state_ == EngineState::CountIn && bar_.position() == 1) {
        state_ = EngineState::Locked; barNumber_ = 1; relockFlag_ = true;
        confidence_ = 0.6f;
    }
    if (out_) {
        EngineEvent ev; ev.type = EngineEvent::Beat;
        ev.beat = { (int64_t) std::llround(pubPhi_), bar_.position(), barNumber_, confidence_,
                    (uint8_t) state_, (uint8_t) (relockFlag_ ? 1 : 0) };
        out_->push(ev);
    }
}

void Tracker::closeBeat(int64_t now)
{
    if (hit_ && bestW_ > 0.0f && follow_) {
        // Apply the beat's best measurement (Repp two-process update, ES-01 section 6).
        double alpha = cfg_.profile.alpha, beta = cfg_.profile.beta;
        if (state_ == EngineState::Chase) { beta = cfg_.profile.chaseBeta; alpha = std::max(alpha, 2.5 * beta); } // near critical damping
        if (state_ == EngineState::Locking) { alpha = std::max(alpha, 0.5); beta = std::max(beta, 0.25); }
        phi_ += alpha * bestSw_ * bestE_;
        tau_ = clampTau(tau_ + beta * bestSw_ * bestE_);
    }
    if (hit_) {
        missCount_ = 0; hitMask_ = (uint8_t) ((hitMask_ << 1) | 1); ++lockHits_;
        innov_[innovPos_] = bestE_; innovPos_ = (innovPos_ + 1) % 8; innovN_ = std::min(innovN_ + 1, 8);
        const int sign = bestE_ > 0 ? 1 : -1;
        if (std::fabs(bestE_) > 0.5 * sigmaMeas_) {
            sameSign_ = (sign == lastSign_) ? sameSign_ + 1 : 1; lastSign_ = sign;
            if (state_ == EngineState::Chase && sameSign_ >= 2) chaseBeats_ = kChaseBeats;   // keep chasing while the trend persists
        }
        else { sameSign_ = 0; lastSign_ = 0; }
    } else {
        ++missCount_; hitMask_ = (uint8_t) (hitMask_ << 1); sameSign_ = 0;
        if (state_ == EngineState::Locking) lockHits_ = 0;
    }

    if (state_ == EngineState::Chase && --chaseBeats_ <= 0) state_ = EngineState::Locked;
    if (state_ == EngineState::Locked && sameSign_ >= 3 && follow_) { state_ = EngineState::Chase; chaseBeats_ = kChaseBeats; sameSign_ = 0; }

    bar_.closeBeat(evKick_, evSnare_, evStrong_);
    if (bar_.position() == 1 && state_ != EngineState::CountIn) ++barNumber_;

    switch (state_) {
    case EngineState::Locking:
        if (lockHits_ >= 4) { state_ = EngineState::Locked; relockPending_ = true; }
        else if (missCount_ >= 2) { goIdle(); return; }
        break;
    case EngineState::Locked:
    case EngineState::Chase:
        if (missCount_ >= kHoldAfterMissed) { state_ = EngineState::Hold; holdStart_ = now; holdOutsideN_ = 0; }
        break;
    case EngineState::CountIn:
        if (missCount_ >= 4) { goIdle(); return; }
        break;
    default: break;
    }
    if (state_ != EngineState::Hold) updateConfidence();

    phi_ += tau_;
    emitted_ = false; hit_ = false; bestE_ = 0; bestW_ = 0; evKick_ = evSnare_ = evStrong_ = false;
}

void Tracker::updateConfidence()
{
    int hits = 0; for (int i = 0; i < 8; ++i) hits += (hitMask_ >> i) & 1;
    const float cHit = hits / 8.0f;
    const float cTempo = induction_ ? induction_->stability() : 0.0f;
    double rms = 0; for (int i = 0; i < innovN_; ++i) rms += innov_[i] * innov_[i];
    rms = innovN_ > 0 ? std::sqrt(rms / innovN_) : 0.0;
    const double z = rms / (0.1 * tau_);
    const float cInno = (float) std::exp(-z * z);
    confidence_ = std::clamp(0.5f * cHit + 0.3f * cTempo + 0.2f * cInno, 0.05f, 1.0f);
}

void Tracker::maybeStartLocking(int64_t now)
{
    if (!induction_ || induction_->numPeaks() == 0) return;
    if (induction_->stableSeconds() < 2.0) return;
    int recent = 0;
    for (int i = 0; i < histN_; ++i) if ((double) (now - hist_[i].t) <= 2.0 * sr_) ++recent;
    if (recent < 3) return;

    double bpm = induction_->bestBpm();
    if (lastBpm_ > 0) {   // prefer the octave consistent with the last tracked tempo
        for (double f : { 0.5, 2.0 })
            if (std::fabs(bpm * f - lastBpm_) < 0.04 * lastBpm_ && induction_->magnitudeAt(bpm * f) > 0.3f * induction_->magnitudeAt(bpm)) bpm *= f;
    }
    const double tau = clampTau(60.0 * sr_ / bpm);
    // Phase search: comb of candidate phases against the recent onset history.
    double bestScore = -1, bestC = 0;
    const int steps = 64;
    for (int c = 0; c < steps; ++c) {
        const double cand = tau * c / steps;
        double score = 0;
        for (int i = 0; i < histN_; ++i) {
            if ((double) (now - hist_[i].t) > kHistSec * sr_) continue;
            double d = std::fmod((double) hist_[i].t - cand, tau); if (d < 0) d += tau;
            if (d > tau / 2) d -= tau;
            score += hist_[i].s * gauss(d, 0.1 * tau) * std::exp(-(double) (now - hist_[i].t) / (1.5 * sr_));
        }
        if (score > bestScore) { bestScore = score; bestC = cand; }
    }
    double phi = bestC + std::ceil(((double) now - bestC) / tau) * tau;
    if (phi <= (double) now) phi += tau;
    tau_ = tau;
    startBeatAt(phi, EngineState::Locking, 1, 1);
    bar_.setPosition(1, false);
}

// ---------------------------------------------------------------- output and actions

void Tracker::fillSnapshot(BeatMapSnapshot& s) const
{
    s.bpm = state_ == EngineState::Idle ? (induction_ ? induction_->bestBpm() : 0.0) : pubBpm_;
    s.beatOriginSample = (int64_t) std::llround(pubPhi_);
    s.nextBeatSample = (int64_t) std::llround(pubPhi_ + pubTau_);
    s.beatInBar = bar_.position();
    s.barNumber = barNumber_;
    s.confidence = state_ == EngineState::Idle ? 0.0f : confidence_;
    s.state = (uint8_t) state_;
    s.flags = (uint8_t) ((relockFlag_ ? SnapshotFlags::Relock : 0) | (octaveFlag_ ? SnapshotFlags::OctaveAmbiguity : 0)
                         | (bar_.confirmed() ? SnapshotFlags::BarConfirmed : 0));
}

void Tracker::tap(int64_t sample)
{
    tick(sample);
    if (lastTap_ >= 0) {
        const double ioi = (double) (sample - lastTap_);
        if (ioi >= minTau_ && ioi <= maxTau_) tau_ = ioi;
    }
    lastTap_ = sample;
    if (tau_ <= 0) tau_ = clampTau(0.5 * sr_);
    const int pos = state_ == EngineState::Idle ? 1 : bar_.position();
    startBeatAt((double) sample, EngineState::Locked, pos, std::max(1, barNumber_));
    bar_.setPosition(pos, false);
}

void Tracker::downbeatNow()
{
    if (state_ == EngineState::Idle) return;
    bar_.setPosition(1, true);
    relockPending_ = true;
}

void Tracker::shiftBar(int delta) { if (state_ != EngineState::Idle) bar_.shift(delta); }

void Tracker::relockNow()
{
    if (state_ == EngineState::Idle) return;
    // Snap the published timeline to the internal estimate at the next beat.
    relockPending_ = true;
    if (state_ == EngineState::Hold) { state_ = EngineState::Locked; missCount_ = 0; }
}

void Tracker::scalePeriod(double factor)
{
    if (state_ == EngineState::Idle || tau_ <= 0) return;
    tau_ = clampTau(tau_ * factor);
    relockPending_ = true;
    octaveSince_ = -1; octaveFlag_ = false;
}

void Tracker::setFollow(bool follow) { follow_ = follow; }

} // namespace pacemaker
