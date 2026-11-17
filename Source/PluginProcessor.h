#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "GaleDSP.h"

class GaleProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    GaleProcessor();
    ~GaleProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gale"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    // Presets (Version 1): sie setzen nur die Regler
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::StringArray envNames();
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> uiTheme { 0 }, uiWidth { 680 }, uiHeight { 420 };
    std::atomic<float> activity { 0.0f };   // Boeen-Aktivitaet 0..1 (fuer die LED im Editor)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float value) override;

    gale::Engine engine;
    std::vector<float> scratchL, scratchR;   // fuer Mono-Spuren (vorab angelegt)
    static_assert (std::atomic<uint32_t>::is_always_lock_free, "Trigger counter must be lock-free");
    std::atomic<uint32_t> triggerSerial { 0 };
    std::atomic<bool> triggerHigh { false };
    uint32_t consumedTriggerSerial = 0; // owned by the audio lifecycle / callback thread
    std::atomic<int> currentProgram { 0 };
    std::atomic<float>* pGust = nullptr; std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pEnv = nullptr; std::atomic<float>* pShelter = nullptr; std::atomic<float>* pHowl = nullptr;
    std::atomic<float>* pBase = nullptr; std::atomic<float>* pOut = nullptr; std::atomic<float>* pBlow = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GaleProcessor)
};
