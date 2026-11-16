#include "AudioBridge.h"
#include <algorithm>

namespace pacemaker {

void AudioBridge::audioDeviceAboutToStart(juce::AudioIODevice* d)
{
    deviceName_ = d->getName();
    const double sr = d->getCurrentSampleRate();
    const int buf = d->getCurrentBufferSizeSamples();
    const int inCh = d->getActiveInputChannels().countNumberOfSetBits();
    sampleRate_ = sr; bufferSize_ = buf;
    reportedIn_ = d->getInputLatencyInSamples(); reportedOut_ = d->getOutputLatencyInSamples();
    core_.prepare(sr, std::max(buf, 2048), inCh);
    int hiddenIn = 0, hiddenOut = 0;
    if (lookup_) lookup_(deviceName_, sr, buf, hiddenIn, hiddenOut);
    core_.setDeviceLatency(reportedIn_.load(), reportedOut_.load(), hiddenIn, hiddenOut);
    zeros_.assign((size_t) std::max(buf, 2048) + 16, 0.0f);
    scratchL_.assign(zeros_.size(), 0.0f); scratchR_ = scratchL_; scratchCal_ = scratchL_;
    inPtrs_.assign((size_t) std::max(inCh, 1) + 1, nullptr);
    midiScratch_.reserve(1024);
    core_.notifyDeviceBack();
    running_ = true;
}

void AudioBridge::audioDeviceStopped()
{
    running_ = false;
    core_.notifyDeviceLost();
}

void AudioBridge::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.getRawDataSize() < 1 || m.isSysEx() || m.getRawData()[0] >= 0xF0) return;
    const uint8_t* d = m.getRawData();
    Raw r { juce::Time::getMillisecondCounterHiRes() * 0.001, d[0], m.getRawDataSize() > 1 ? d[1] : (uint8_t) 0, m.getRawDataSize() > 2 ? d[2] : (uint8_t) 0 };
    midiQ_.push(r);
}

void AudioBridge::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                   const juce::AudioIODeviceCallbackContext& ctx)
{
    ++callbacks_;
    for (int c = 0; c < numOut; ++c) if (out[c]) std::fill(out[c], out[c] + n, 0.0f);
    if (n > (int) zeros_.size() - 16) return;

    const double blockStartSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double sr = sampleRate_.load();
    midiScratch_.clear();
    Raw r;
    while (midiQ_.pop(r)) {
        CoreMidiIn m;
        m.offset = std::clamp((int) ((r.t - blockStartSec) * sr), 0, n - 1);
        m.status = r.s; m.d1 = r.d1; m.d2 = r.d2;
        midiScratch_.push_back(m);
    }

    // Compact the *active* input channels (JUCE gives null for inactive ones in the middle).
    int nIn = 0;
    for (int c = 0; c < numIn && nIn < (int) inPtrs_.size(); ++c) inPtrs_[(size_t) nIn++] = in[c] ? in[c] : zeros_.data();

    CoreHostInfo h;
    h.hostTimeUs = -1;
#if JUCE_MAC
    if (ctx.hostTimeNs != nullptr) h.hostTimeUs = (int64_t) (*ctx.hostTimeNs / 1000);   // mach time == steady_clock on macOS
#else
    (void) ctx;
#endif
    h.outputLatencyUs = 0;

    const int sL = syncL_.load(), sR = syncR_.load(), cO = calOut_.load();
    std::fill(scratchL_.begin(), scratchL_.begin() + n, 0.0f);
    std::fill(scratchR_.begin(), scratchR_.begin() + n, 0.0f);
    std::fill(scratchCal_.begin(), scratchCal_.begin() + n, 0.0f);
    core_.process(inPtrs_.data(), nIn, n, h, midiScratch_.data(), (int) midiScratch_.size(),
                  (sL >= 0 || sR >= 0) ? scratchL_.data() : nullptr, (sL >= 0 || sR >= 0) ? scratchR_.data() : nullptr,
                  cO >= 0 ? scratchCal_.data() : nullptr, nullptr, 0);
    auto put = [&](int ch, const std::vector<float>& src) { if (ch >= 0 && ch < numOut && out[ch]) for (int i = 0; i < n; ++i) out[ch][i] += src[(size_t) i]; };
    put(sL, scratchL_); put(sR, scratchR_); put(cO, scratchCal_);
}

} // namespace pacemaker
