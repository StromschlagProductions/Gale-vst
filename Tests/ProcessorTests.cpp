#include "PluginProcessor.h"
#include "TestSupport.h"
#include <memory>
#include <thread>

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


    // Actual output must match a reference engine with exactly the intended event count.
    for (int channels : { 1, 2 })
    for (int block : { 64, 8192, 32769 })
    {
        GaleProcessor proc;
        auto buses = proc.getBusesLayout();
        auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        buses.inputBuses.set (0, set); buses.outputBuses.set (0, set);
        CHECK (proc.setBusesLayout (buses)); proc.prepareToPlay (48000, 64);
        gale::Engine reference; reference.prepare (48000);
        gale::Params params; params.trigger = false;
        juce::AudioBuffer<float> b (channels, block);
        std::vector<float> l ((size_t) block), r ((size_t) block);
        auto* trigger = proc.apvts.getParameter ("trigger");
        auto pulse = [&] { trigger->setValueNotifyingHost (1); trigger->setValueNotifyingHost (0); };
        auto process = [&] (unsigned events)
        {
            b.clear(); std::fill (l.begin(), l.end(), 0); std::fill (r.begin(), r.end(), 0);
            { NoAllocations guard; proc.processBlock (b, midi); }
            reference.process (l.data(), r.data(), block, params, events);
            for (int i = 0; i < block; ++i)
            {
                const auto at = (size_t) i;
                const float expected = channels == 1 ? 0.70710678f * (l[at] + r[at]) : l[at];
                CHECK (b.getSample (0, i) == expected);
                if (channels == 2) CHECK (b.getSample (1, i) == r[at]);
            }
        };
        trigger->setValueNotifyingHost (1); process (1); process (0); process (0);
        trigger->setValueNotifyingHost (0); process (0);
        pulse(); process (1); pulse(); process (1);
        pulse(); pulse(); pulse(); process (3);
        pulse(); proc.reset(); reference.reset(); process (0);
        pulse(); proc.prepareToPlay (48000, 64); reference.prepare (48000); process (0);
        trigger->setValueNotifyingHost (1);
        proc.prepareToPlay (48000, 64); reference.prepare (48000); process (1); process (0);
        CHECK (proc.getLatencySamples() == 0);
    }
    {
        GaleProcessor first, second;
        first.setCurrentProgram (4); first.uiTheme = 1; first.uiWidth = 1020; first.uiHeight = 630;
        juce::MemoryBlock state; first.getStateInformation (state);
        second.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (second.getCurrentProgram() == 4 && second.uiTheme == 1 && second.uiWidth == 1020);
        second.setStateInformation ("invalid", 7); CHECK (second.uiWidth == 1020);
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> a (first.createEditor()), b (second.createEditor());
            a->setSize (680, 420); b->setSize (1360, 840);
            CHECK (first.uiWidth == 680 && second.uiWidth == 1360);
        }
    }

    // Host bool values inside the same snapped bin still need canonical restore.
    for (const auto* id : { "blow", "trigger" })
    {
        GaleProcessor proc;
        auto* parameter = proc.apvts.getParameter (id);
        parameter->setValueNotifyingHost (1.0f);
        juce::MemoryBlock state; proc.getStateInformation (state);
        parameter->setValueNotifyingHost (0.839735f);
        proc.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (parameter->getValue() == 1.0f);
        parameter->setValueNotifyingHost (0.0f); proc.getStateInformation (state);
        parameter->setValueNotifyingHost (0.14f);
        proc.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (parameter->getValue() == 0.0f);
    }

    // Parameter / state traffic while independent instances are processing.
    {
        GaleProcessor first, second; first.prepareToPlay (48000, 127); second.prepareToPlay (48000, 127);
        juce::MemoryBlock saved; first.getStateInformation (saved);
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> b (2, 127); juce::MidiBuffer m;
            for (int i = 0; i < 200; ++i)
            {
                b.clear(); { NoAllocations guard; first.processBlock (b, m); second.processBlock (b, m); }
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 127; ++n) CHECK (std::isfinite (b.getSample (c, n)));
            }
        });
        for (int i = 0; i < 100; ++i)
        {
            first.setStateInformation (saved.getData(), (int) saved.getSize());
            second.setCurrentProgram (i % second.getNumPrograms());
            first.uiWidth = i % 2 ? 680 : 1020;
            juce::MemoryBlock copy; first.getStateInformation (copy);
        }
        audio.join();
    }
    CHECK (p->getTailLengthSeconds() > 0.0);
    std::puts ("Gale processor: layouts, passthrough, bounded callbacks, mono, state roundtrip, presets and click trigger passed");
}
