// Pacemaker audio plugin processor (ES-04 section 4): buses, parameters, state, MIDI, host transport.
#pragma once
#include "PortManager.h"
#include "UserSettingsStore.h"
#include "pacemaker/Core.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pacemaker {

class PacemakerProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer {
public:
    PacemakerProcessor();
    ~PacemakerProcessor() override;

    // AudioProcessor
    const juce::String getName() const override { return "Pacemaker"; }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    Core& core() { return core_; }
    MidiPortManager& ports() { return ports_; }
    UserSettingsStore& store() { return store_; }
    UserSettings& userSettings() { return user_; }
    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    juce::StringArray inputChannelNames() const;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // For tests: how many input channels the current layout feeds the core.
    int flatInputChannels() const { return flatChannels_; }

private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    Params paramsFromApvts() const;
    void pushParamsToApvts(const Params& p);
    void rebuildDefaultRoles();
    int countFlatChannels() const;

    Core core_;
    MidiPortManager ports_;
    UserSettingsStore store_ { "Pacemaker" };
    UserSettings user_;
    juce::AudioProcessorValueTreeState apvts_;
    std::atomic<bool> paramsDirty_ { false };
    bool ignoreParamEvents_ = false;
    Params lastApplied_;
    int flatChannels_ = 0;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    std::vector<const float*> inPtrs_;
    std::vector<CoreMidiIn> midiIn_;
    std::vector<CoreMidiOut> midiOut_;
    std::vector<float> silence_;
    int ticks_ = 0;
    std::atomic<int> tapPrev_ { 0 }, dbPrev_ { 0 }, relockPrev_ { 0 }, halfPrev_ { 0 }, dblPrev_ { 0 };
    int validHostTimeBlocks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PacemakerProcessor)
};

} // namespace pacemaker
