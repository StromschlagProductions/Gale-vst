#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

juce::StringArray GaleProcessor::envNames()
{
    return { "Open Land", "Trees & Scrub", "Rocks & Canyon", "Cracks & Doors" };   // Reihenfolge wie gale::envCfg()
}

namespace
{
constexpr float kBaseMin = -60.0f, kBaseMax = -12.0f;   // BASE LEVEL in dB, ganz links = aus

struct Preset { const char* name; int env; float gust, autoGust, shelter, howl, baseDb; };
const Preset kPresets[] = {
    { "Outback Stillness",          0, 0.12f, 0.15f, 0.00f, 0.20f, -32.0f },
    { "Sudden Desert Gust",         0, 0.12f, 0.00f, 0.00f, 0.50f, -32.0f },
    { "Howling Canyon",             2, 0.55f, 0.50f, 0.00f, 0.80f, -32.0f },
    { "Storm Outside Cabin",        1, 0.55f, 0.60f, 0.55f, 0.50f, -32.0f },
    { "Drafty Doorway",             3, 0.45f, 0.00f, 0.30f, 0.80f, -32.0f },
    { "Approaching Tempest",        0, 0.60f, 0.70f, 0.00f, 0.60f, -32.0f },
    { "Blechlabor im Sturm",        0, 0.55f, 0.70f, 0.62f, 0.60f, -32.0f },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
}

juce::AudioProcessorValueTreeState::ParameterLayout GaleProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto decibel = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (v, 1) + " dB"; });
    auto autoText = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { const int pct = roundToInt (v * 100.0f); return pct <= 0 ? String ("aus") : String (pct) + " %"; });
    auto shelterText = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { const int pct = roundToInt (v * 100.0f);
                            return pct <= 0 ? u8 ("drau\xc3\x9f" "en") : pct >= 100 ? String ("drinnen") : String (pct) + " %"; });
    auto baseText = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return v <= kBaseMin + 0.05f ? String ("aus") : String (v, 1) + " dB"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gust", 1 }, "Gust / Intensity",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "trigger", 1 }, "Trigger Gust", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "autoGust", 1 }, "Auto Gust",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.0f, autoText));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "env", 1 }, "Environment", envNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "shelter", 1 }, "Shelter",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.0f, shelterText));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "howl", 1 }, "Howl",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "baseLevel", 1 }, "Base Level",
                                                       NormalisableRange<float> (kBaseMin, kBaseMax), -32.0f, baseText));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f), 0.0f, decibel));
    return layout;
}

GaleProcessor::GaleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GALE", createLayout())
{
    pGust = apvts.getRawParameterValue ("gust");
    pTrigger = apvts.getRawParameterValue ("trigger");
    pAuto = apvts.getRawParameterValue ("autoGust");
    pEnv = apvts.getRawParameterValue ("env");
    pShelter = apvts.getRawParameterValue ("shelter");
    pHowl = apvts.getRawParameterValue ("howl");
    pBase = apvts.getRawParameterValue ("baseLevel");
    pOut = apvts.getRawParameterValue ("output");
    apvts.addParameterListener ("trigger", this);
}

GaleProcessor::~GaleProcessor() { apvts.removeParameterListener ("trigger", this); }

void GaleProcessor::parameterChanged (const juce::String&, float value)
{
    if (value > 0.5f) triggerLatch.store (true);
}

void GaleProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    engine.prepare (sampleRate);
    const size_t n = (size_t) juce::jmax (maxBlock, 4096);
    scratchL.assign (n, 0.0f);
    scratchR.assign (n, 0.0f);
    setLatencySamples (0);
}

bool GaleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in == out) return true;
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();   // Mono rein, Stereo raus
}

void GaleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    gale::Params p;
    p.gust = pGust->load();
    p.autoGust = pAuto->load();
    p.trigger = pTrigger->load() > 0.5f || triggerLatch.exchange (false);
    p.env = juce::roundToInt (gale::finiteClamp (pEnv->load(), 0, gale::kNumEnvs - 1, 0));
    p.shelter = pShelter->load();
    p.howl = pHowl->load();
    const float base = pBase->load();
    p.baseDb = base <= kBaseMin + 0.05f ? -80.0f : base;   // ganz links = aus
    p.outDb = pOut->load();

    float* left = buffer.getWritePointer (0);

    if (numCh > 1)
    {
        // Mono rein, Stereo raus: den Eingang auf beide Seiten legen, der Wind kommt dazu
        if (getTotalNumInputChannels() == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        engine.process (left, buffer.getWritePointer (1), numSamples, p);
    }
    else
    {
        // reine Mono-Spur: Wind links/rechts zusammenmischen (in vorreservierten Abschnitten)
        if (scratchL.empty()) { buffer.clear(); return; }
        const bool trig = p.trigger;
        for (int offset = 0; offset < numSamples;)
        {
            const int count = std::min ((int) scratchL.size(), numSamples - offset);
            std::fill_n (scratchL.data(), count, 0.0f);
            std::fill_n (scratchR.data(), count, 0.0f);
            p.trigger = trig && offset == 0;
            engine.process (scratchL.data(), scratchR.data(), count, p);
            for (int i = 0; i < count; ++i)
                left[offset + i] += 0.70710678f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
            offset += count;
        }
    }

    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear (ch, 0, numSamples);

    activity.store (engine.activity());
}

//==============================================================================
int GaleProcessor::getNumPrograms() { return kNumPresets; }

const juce::String GaleProcessor::getProgramName (int i)
{
    return i >= 0 && i < kNumPresets ? juce::String (kPresets[i].name) : juce::String();
}

void GaleProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= kNumPresets) return;
    currentProgram = i;
    const auto& pr = kPresets[i];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("env", (float) pr.env);
    set ("gust", pr.gust);
    set ("autoGust", pr.autoGust);
    set ("shelter", pr.shelter);
    set ("howl", pr.howl);
    set ("baseLevel", pr.baseDb);
}

void GaleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GaleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* GaleProcessor::createEditor() { return new GaleEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GaleProcessor(); }
