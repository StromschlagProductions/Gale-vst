#include "PluginProcessor.h"
#include "TestSupport.h"
#include <memory>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<GaleProcessor>();

    // Stereo (Standard), Mono rein/Stereo raus und reine Mono-Spur werden akzeptiert, Surround nicht
    auto layout = p->getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    CHECK (p->setBusesLayout (layout));
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    CHECK (p->setBusesLayout (layout));
    layout.inputBuses.set (0, juce::AudioChannelSet::create5point1());
    layout.outputBuses.set (0, juce::AudioChannelSet::create5point1());
    CHECK (! p->setBusesLayout (layout));
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    CHECK (p->setBusesLayout (layout));
    p->prepareToPlay (48000, 64);

    juce::MidiBuffer midi;

    // Stereo: Eingang laeuft durch, Wind kommt dazu; ohne Wind (alles aus) bleibt der Eingang bit-genau
    {
        for (auto* id : { "gust", "autoGust", "baseLevel" })
        {
            auto* prm = p->apvts.getParameter (id);
            prm->setValueNotifyingHost (0.0f);   // gust 0, auto aus, base level ganz links = aus
        }
        juce::AudioBuffer<float> b (2, 4096);
        for (int i = 0; i < 4096; ++i) { const float v = 0.2f * std::sin (0.03f * (float) i); b.setSample (0, i, v); b.setSample (1, i, -v); }
        const auto ref = b;
        { NoAllocations guard; p->processBlock (b, midi); }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4096; ++i) CHECK (b.getSample (c, i) == ref.getSample (c, i));
    }

    // Standardwerte laden: es kommt Wind, endlich und begrenzt, bei jeder Blockgroesse ohne Speicheranforderung
    p->apvts.getParameter ("gust")->setValueNotifyingHost (p->apvts.getParameter ("gust")->convertTo0to1 (0.5f));
    p->apvts.getParameter ("baseLevel")->setValueNotifyingHost (p->apvts.getParameter ("baseLevel")->convertTo0to1 (-32.0f));
    {
        juce::AudioBuffer<float> b (2, 8193);
        double energy = 0;
        for (int round = 0; round < 40; ++round)
            for (int n : { 0, 1, 7, 31, 64, 513, 8193 })
            {
                b.setSize (2, n, false, false, true);
                b.clear();
                { NoAllocations guard; p->processBlock (b, midi); }
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i) { const float x = b.getSample (c, i); CHECK (std::isfinite (x) && std::abs (x) <= 1.05f); energy += (double) x * x; }
            }
        CHECK (energy > 0.0);
    }

    // Reine Mono-Spur, auch mit einem Block, der groesser ist als vorbereitet
    {
        layout.inputBuses.set (0, juce::AudioChannelSet::mono());
        layout.outputBuses.set (0, juce::AudioChannelSet::mono());
        CHECK (p->setBusesLayout (layout));
        p->prepareToPlay (48000, 64);
        juce::AudioBuffer<float> b (1, 32769);
        b.clear();
        { NoAllocations guard; p->processBlock (b, midi); }
        double e = 0;
        for (int i = 0; i < b.getNumSamples(); ++i) { const float x = b.getSample (0, i); CHECK (std::isfinite (x)); e += (double) x * x; }
        CHECK (e > 0.0);
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        CHECK (p->setBusesLayout (layout));
        p->prepareToPlay (48000, 64);
    }

    // Zustand speichern und laden
    {
        auto* env = p->apvts.getParameter ("env");
        auto* howl = p->apvts.getParameter ("howl");
        env->setValueNotifyingHost (env->convertTo0to1 (2));
        howl->setValueNotifyingHost (howl->convertTo0to1 (0.83f));
        juce::MemoryBlock state;
        p->getStateInformation (state);
        env->setValueNotifyingHost (env->convertTo0to1 (0));
        howl->setValueNotifyingHost (howl->convertTo0to1 (0.1f));
        p->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (std::abs (p->apvts.getRawParameterValue ("env")->load() - 2.0f) < 1e-5f);
        CHECK (std::abs (p->apvts.getRawParameterValue ("howl")->load() - 0.83f) < 1e-4f);
    }

    // Presets: alle sieben setzen ihre Regler, danach laeuft der Prozessor weiter sauber
    {
        CHECK (p->getNumPrograms() == 7);
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            CHECK (p->getProgramName (i).isNotEmpty());
            p->setCurrentProgram (i);
            CHECK (p->getCurrentProgram() == i);
        }
        p->setCurrentProgram (6);   // Blechlabor im Sturm
        CHECK (std::abs (p->apvts.getRawParameterValue ("shelter")->load() - 0.62f) < 1e-4f);
        p->setCurrentProgram (2);   // Howling Canyon
        CHECK (juce::roundToInt (p->apvts.getRawParameterValue ("env")->load()) == 2);
        p->setCurrentProgram (-1);  // ungueltig: ignorieren
        p->setCurrentProgram (99);
        CHECK (p->getCurrentProgram() == 2);
    }

    // TRIGGER GUST: auch ein sehr kurzer Klick (an und gleich wieder aus, vor dem naechsten Block) loest eine Boee aus
    {
        auto* trig = p->apvts.getParameter ("trigger");
        p->apvts.getParameter ("autoGust")->setValueNotifyingHost (0.0f);
        p->apvts.getParameter ("baseLevel")->setValueNotifyingHost (0.0f);   // Grundwind aus, damit nur die Boee gemessen wird
        auto* gust = p->apvts.getParameter ("gust");
        gust->setValueNotifyingHost (gust->convertTo0to1 (0.05f));
        p->apvts.getParameter ("shelter")->setValueNotifyingHost (0.0f);
        p->prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512);
        auto energy = [&] (int blocks)
        {
            double e = 0;
            for (int k = 0; k < blocks; ++k) { b.clear(); p->processBlock (b, midi); for (int i = 0; i < 512; ++i) e += (double) b.getSample (0, i) * b.getSample (0, i); }
            return e;
        };
        energy (200);                        // einschwingen
        const double calm = energy (60);
        trig->setValueNotifyingHost (1.0f);  // Klick: an ...
        trig->setValueNotifyingHost (0.0f);  // ... und aus, noch bevor ein Block lief
        const double gustE = energy (60);    // ca. 0,64 s: Anstieg und Gipfel
        CHECK (gustE > calm * 20.0);   // mindestens +13 dB schon im ersten Anstieg (0,64 s)
        CHECK (p->activity.load() > -1.0f);
    }

    CHECK (p->getTailLengthSeconds() > 0.0);
    std::puts ("Gale processor: layouts, passthrough, bounded callbacks, mono, state roundtrip, presets and click trigger passed");
}
