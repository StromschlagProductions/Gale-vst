// screenshot.cpp - rendert die echte Plugin-Oberflaeche als PNG (fuer die README). Aufruf: GaleScreenshot <Ordner>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String dir = argc > 1 ? argv[1] : ".";
    for (int theme = 0; theme < 2; ++theme)
    {
        GaleProcessor p;
        p.apvts.state.setProperty ("theme", theme, nullptr);
        p.setCurrentProgram (6);   // Blechlabor im Sturm: Beispieleinstellung fuer den Screenshot
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (680, 420);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out (dir + (theme == 0 ? "/oberflaeche-dunkel.png" : "/oberflaeche-hell.png"));
        out.deleteFile();
        juce::FileOutputStream fo (out);
        if (fo.openedOk()) juce::PNGImageFormat().writeImageToStream (img, fo);
    }
    return 0;
}
