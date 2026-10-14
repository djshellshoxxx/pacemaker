// Plugin-level tests without a host: every accepted bus layout processes audio (A3), state round trips (A4).
#include "PluginProcessor.h"
#include <cstdio>

using namespace pacemaker;

static int failures = 0;
#define CHECKP(c) do { if (!(c)) { std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void layoutMatrix()
{
    PacemakerProcessor proc;
    using Set = juce::AudioChannelSet;
    const Set opts[] = { Set::disabled(), Set::mono(), Set::stereo() };
    int accepted = 0, rejected = 0;
    for (const auto& mainIn : opts) for (const auto& trig : opts) for (const auto& kick : opts) for (const auto& snare : opts)
    for (const auto& mainOut : { Set::mono(), Set::stereo() }) for (const auto& sync : { Set::disabled(), Set::stereo() }) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses = { mainIn, trig, kick, snare };
        l.outputBuses = { mainOut, sync };
        if (!proc.isBusesLayoutSupported(l)) { ++rejected; continue; }
        if (!proc.setBusesLayout(l)) continue;                    // JUCE may refuse combinations the processor allows
        ++accepted;
        proc.setPlayConfigDetails(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 48000.0, 256);
        proc.prepareToPlay(48000.0, 256);
        const int chans = juce::jmax(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(juce::jmax(1, chans), 256);
        juce::MidiBuffer midi;
        juce::Random rnd(1);
        // 10 s of audio with a 120 BPM kick-like pulse so the engine does real work in every layout.
        for (int b = 0; b < 48000 * 10 / 256; ++b) {
            buf.clear();
            for (int c = 0; c < buf.getNumChannels(); ++c) for (int i = 0; i < 256; ++i) {
                const long long t = (long long) b * 256 + i;
                const double ph = (double) (t % 24000) / 48000.0;
                buf.setSample(c, i, (float) (ph < 0.02 ? 0.8 * std::sin(ph * 2 * juce::MathConstants<double>::pi * 80.0) * std::exp(-ph / 0.01) : 0.0) + 0.001f * (rnd.nextFloat() - 0.5f));
            }
            midi.clear();
            if (b % 94 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 100), 10);
            proc.processBlock(buf, midi);
        }
        proc.releaseResources();
    }
    std::printf("A3 bus layouts: %d accepted and processed, %d rejected by isBusesLayoutSupported\n", accepted, rejected);
    CHECKP(accepted >= 12);
}

static void stateRoundTrip()
{
    PacemakerProcessor a;
    a.prepareToPlay(48000.0, 256);
    a.apvts().getParameter("referenceBpm")->setValueNotifyingHost(a.apvts().getParameter("referenceBpm")->convertTo0to1(133.0f));
    a.core().editSession([](SessionSettings& s) {
        s.osc.host = "10.1.2.3"; s.osc.port = 9100; s.midi.offsetMs = -7.5; s.midiPort = "My Port"; s.link.quantum = 8; s.inputLatencyMs = 12;
        s.songs.songs = { { "One", 100, 4, 4, BarPattern::Rock44, 4, 3 } }; s.autoRoleMap = false;
        s.channels = { { 0, InputRole::Snare, 0.2, false } };
    });
    juce::MemoryBlock mb; a.getStateInformation(mb);
    PacemakerProcessor b;
    b.prepareToPlay(48000.0, 256);
    b.setStateInformation(mb.getData(), (int) mb.getSize());
    const auto sa = a.core().session(), sb = b.core().session();
    CHECKP(sb.osc.host == "10.1.2.3" && sb.osc.port == 9100);
    CHECKP(sb.midi.offsetMs == -7.5 && sb.midiPort == "My Port" && sb.link.quantum == 8);
    CHECKP(sb.songs.songs.size() == 1 && sb.songs.songs[0].programChange == 3);
    CHECKP(sb.channels.size() == 1 && sb.channels[0].role == InputRole::Snare);
    CHECKP(std::abs(b.core().params().referenceBpm - 133.0) < 0.2);
    // Unknown / garbage / future state must not crash.
    const char junk[] = "not xml at all";
    b.setStateInformation(junk, (int) sizeof junk);
    juce::XmlElement x(a.apvts().state.getType());
    x.setAttribute("future", 1);
    auto* pm = x.createNewChildElement("PM_SETTINGS");
    pm->setAttribute("json", "{\"stateVersion\":99,\"session\":{\"osc\":{\"host\":\"9.9.9.9\"},\"alien\":1}}");
    juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary(x, mb2);
    b.setStateInformation(mb2.getData(), (int) mb2.getSize());
    CHECKP(b.core().session().osc.host == "9.9.9.9");
    std::printf("A4 plugin state round trip done\n");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    layoutMatrix();
    stateRoundTrip();
    std::printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
