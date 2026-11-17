#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* kThemeProp = "theme";   // 0 = dunkel, 1 = hell (wird mit dem Projekt gespeichert)
const char* kUiWProp = "uiW";       // Fenstergroesse (wird mit dem Projekt gespeichert)
const char* kUiHProp = "uiH";

const char* kEnvShort[gale::kNumEnvs] = { "Open Land", "Trees", "Rocks", "Cracks" };
const char* kEnvNote[gale::kNumEnvs] = {
    "Weite Fl\xc3\xa4" "che, Steppe, K\xc3\xbcste: tiefes Rauschen, weite Druckwellen",
    "B\xc3\xa4ume, B\xc3\xbcsche, Laub: weiches Rauschen der Bl\xc3\xa4tter",
    "Fels und Schlucht: hohl, jault bei Windst\xc3\xa4rke nach oben",
    "Ritzen, T\xc3\xbcren, Fenster: schneidendes Pfeifen und Zugluft" };
}

// Farben wie bei den anderen Stromschlag-Plugins (Schiefer-Gruen + Orange)
GaleEditor::Theme GaleEditor::darkTheme()
{
    return { juce::Colour (0xff182326), juce::Colour (0xff233539), juce::Colour (0xfff5781a),
             juce::Colour (0xffe8ecec), juce::Colour (0xff8fa1a5), juce::Colour (0xff35494e), true };
}
GaleEditor::Theme GaleEditor::lightTheme()
{
    return { juce::Colour (0xffd5dbda), juce::Colour (0xffe8eae9), juce::Colour (0xfff5781a),
             juce::Colour (0xff233539), juce::Colour (0xff5d6e72), juce::Colour (0xffb5bfbe), false };
}

// Inhalt der Onboard-Hilfe, in der Reihenfolge der Oberflaeche
static juce::AttributedString buildHelp (juce::Colour text, juce::Colour accent, juce::Colour dim)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.setJustification (juce::Justification::topLeft);

    auto head = [&] (const char* t)
    { s.append (u8 (t) + "\n", juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), accent); };
    auto body = [&] (const char* t)
    { s.append (u8 (t) + "\n\n", juce::Font (juce::FontOptions (13.0f)), text); };
    auto note = [&] (const char* t)
    { s.append (u8 (t) + "\n\n", juce::Font (juce::FontOptions (12.0f)), dim); };

    s.append (u8 ("GALE \xe2\x80\x93 Hilfe") + u8 (" \xc2\xb7 v") + juce::String (PLUGIN_VERSION_TEXT) + "\n\n",
              juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), accent);

    head ("Einrichten");
    body ("GALE erzeugt den Wind selbst, ohne Samples. Am besten auf einen FX-Kanal oder Bus legen und von dort in Spatial Lab oder den Szenenbus schicken. Ein Eingang ist nicht nötig; was am Eingang anliegt, läuft unverändert durch, der Wind kommt dazu. MIDI wird nicht gebraucht, alles geht über Automation.");

    head ("BLOW");
    body ("Schaltet den Wind an oder aus. Beim Einschalten blendet er in etwa 1,5 Sekunden ein, beim Ausschalten legt er sich in etwa 2,5 Sekunden. Der Schalter ist ein Automationsparameter: So lässt du den Wind in einer langen Szene genau dort beginnen und enden, wo er gebraucht wird. Ein Preset schaltet den Wind immer ein.");

    head ("ENVIRONMENT");
    body ("Charakter des Windes: Open Land (weite Fläche, tiefes Rauschen), Trees (weiches Rauschen der Blätter in Bäumen und Büschen, mehrere Äste wogen unabhängig), Rocks (hohl, jault bei Windstärke nach oben) und Cracks (schneidendes Pfeifen und Zugluft durch Ritzen). Beim Umschalten blendet GALE weich über.");

    head ("GUST / INTENSITY");
    body ("Der Hauptregler und das wichtigste Ziel für die Automation. Er bestimmt die laufende Windstärke und steuert gleichzeitig Lautstärke, Luftdruck, Pfeifton und Stereo-Bewegung. Ganz unten bleibt nur der leise Grundwind.");

    head ("TRIGGER GUST");
    body ("Löst eine einzelne Böe aus, die anschwillt und wieder abklingt. Den Verlauf zeichnet GALE selbst. Der Taster ist auch ein Automationsparameter: Ein kurzer Sprung von 0 auf 1 setzt die Böe genau an die Stelle, zum Beispiel auf einen Satz im Dialog. Zum Einspielen die Automation auf Touch oder Write stellen, die Szene laufen lassen und im richtigen Moment klicken. Die LED leuchtet, solange eine Böe läuft.");
    note ("Ein Automationssprung sollte mindestens etwa 50 ms lang sein, also länger als ein Audiopuffer. Ein Klick auf den Taster geht auch bei sehr kurzem Druck nicht verloren.");

    head ("AUTO GUST");
    body ("Lässt zufällig Böen vorbeiziehen, ohne dass du etwas zeichnen musst. Der Regler bestimmt, wie oft und wie kräftig (ganz links aus). Gezielte Böen mit TRIGGER GUST kommen zusätzlich dazu.");

    head ("SHELTER (Innen und Außen)");
    body ("Perspektive: draußen = volles Spektrum und direkte Turbulenzen. Bei etwa 50 % bist du hinter Fensterscheibe oder Zeltwand: Höhen werden gedämpft, tiefes Wummern von Scheibe und Wand tritt hervor. Bei 100 % bist du tief im Gebäude: starker Tiefpass, tiefes Grollen und Pfeifen in den Ritzen. Mit Abstand zum Freien beginnt außerdem das Rappeln von großem Blech (Wellblech, Blechdach, Gehäusewände): tiefe, lang nachschwingende Schläge und schlackernde Platten, die mit den Böen häufiger und kräftiger werden. Dazu drückt der Wind als tiefes, ungleichmäßiges Grollen gegen die Wände.");

    head ("HOWL");
    body ("Heulen und Pfeifen des Windes: Bei 0 % reines Rauschen, bei 100 % ein deutlicher, wandernder Ton, der mit den Böen höher und lauter wird. Wie es klingt, hängt von ENVIRONMENT ab: tiefes Stöhnen bei Open Land, Singen in den Ästen bei Trees, hohles Jaulen bei Rocks (dort wird auch der Grundklang resonanter), schneidendes Pfeifen bei Cracks. Die Gesamtlautstärke bleibt dabei etwa gleich. Bei hohem SHELTER bestimmt HOWL auch das Pfeifen in den Ritzen.");

    head ("BASE LEVEL");
    body ("Lautstärke des dauerhaften Grundwindes, der digitale Stille in Außenszenen verhindert. Der Wert entspricht etwa der Lautheit des Grundwindes in dB. Ganz links aus.");

    head ("OUTPUT");
    body ("Master-Pegel des Windes (der Eingang bleibt unberührt).");

    head ("Presets");
    body ("Outback Stillness, Sudden Desert Gust, Howling Canyon, Storm Outside Cabin, Drafty Doorway, Approaching Tempest und Blechlabor im Sturm. Sie stellen nur die Regler ein und erscheinen in der Preset-Liste deines Hosts.");

    head ("Allgemein");
    body ("Doppelklick auf einen Drehregler oder Strg-Klick setzt ihn auf den Standardwert zurück.");
    body ("Look: Dunkel / Hell umschalten, die Wahl wird mit dem Projekt gespeichert. Das Fenster ist am rechten unteren Rand skalierbar (50–300 %).");
    note ("Alle Regler sind automatisierbar. GALE arbeitet ohne Latenz und ist mono-kompatibel: Links und Rechts sind unabhängige Rauschquellen, die Bewegung der Böen entsteht durch zeitversetzte Steuerkurven, nie durch verzögertes Audio.");

    head ("Idee und Konzept");
    body ("Lars Eickstädt, Stromschlag Audio");
    head ("Beta-Testing");
    body ("Markus Hildebrandt (SOUNDJOB)");
    note ("Gebaut mit JUCE. Das tiefe Druck-Rauschen hinter Wänden beruht auf dem Rauschkern von Airwindows \"Noise\" (Chris Johnson, MIT-Lizenz).");

    return s;
}

GaleEditor::GaleEditor (GaleProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 3, kBaseH * 3);   // 50 % bis 300 %
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    theme = (int) processor.apvts.state.getProperty (kThemeProp, 0) == 1 ? lightTheme() : darkTheme();
    setLookAndFeel (&look);

    setupKnob (gust, "gust", "GUST / INTENSITY");
    setupKnob (shelter, "shelter", "SHELTER");
    setupKnob (howl, "howl", "HOWL");
    setupKnob (baseLevel, "baseLevel", "BASE LEVEL");
    setupKnob (autoGust, "autoGust", "AUTO GUST");
    setupKnob (output, "output", "OUTPUT");

    // ENVIRONMENT: 4-Wege-Wahlschalter
    envLabel.setText ("ENVIRONMENT", juce::dontSendNotification);
    content.addAndMakeVisible (envLabel);
    envNote.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (envNote);
    for (int i = 0; i < gale::kNumEnvs; ++i)
    {
        auto& b = envButtons[i];
        b.setButtonText (kEnvShort[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1);
        b.onClick = [this, i] { if (envAttachment) envAttachment->setValueAsCompleteGesture ((float) i); };
        content.addAndMakeVisible (b);
    }
    envAttachment = std::make_unique<juce::ParameterAttachment> (*processor.apvts.getParameter ("env"),
        [this] (float v) { updateEnvButtons (juce::roundToInt (v)); }, nullptr);
    envAttachment->sendInitialUpdate();

    // BLOW: Wind an/aus
    blowAttachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, "blow", blowButton);
    content.addAndMakeVisible (blowButton);

    // TRIGGER GUST
    triggerButton.onPress = [this] (bool down)
    {
        if (auto* prm = processor.apvts.getParameter ("trigger"))
        {
            if (down) prm->beginChangeGesture();
            prm->setValueNotifyingHost (down ? 1.0f : 0.0f);
            if (! down) prm->endChangeGesture();
        }
    };
    content.addAndMakeVisible (triggerButton);
    triggerNote.setText (u8 ("Wind ein/aus und B\xc3\xb6" "e ausl\xc3\xb6sen \xe2\x80\x93 beides auch als Automation"), juce::dontSendNotification);
    triggerNote.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (triggerNote);

    themeButton.onClick = [this]
    {
        theme = theme.dark ? lightTheme() : darkTheme();
        processor.apvts.state.setProperty (kThemeProp, theme.dark ? 0 : 1, nullptr);
        applyTheme();
    };
    content.addAndMakeVisible (themeButton);

    helpButton.setButtonText ("?");
    helpButton.setTooltip (u8 ("Hilfe: was machen die Regler?"));
    helpButton.onClick = [this] { helpScroll.setVisible (! helpScroll.isVisible()); };
    content.addAndMakeVisible (helpButton);

    helpScroll.setViewedComponent (&helpText, false);
    helpScroll.setScrollBarsShown (true, false);
    helpScroll.setVisible (false);
    content.addChildComponent (helpScroll);   // startet ausgeblendet

    // Tooltips
    gust.slider.setTooltip (u8 ("Laufende Windst\xc3\xa4rke: steuert Lautst\xc3\xa4rke, Luftdruck, Pfeifton und Stereo-Bewegung. Hauptziel f\xc3\xbcr die Automation."));
    blowButton.setTooltip (u8 ("Wind an oder aus. Er blendet weich ein (ca. 1,5 s) und legt sich beim Ausschalten (ca. 2,5 s). Automatisierbar."));
    triggerButton.setTooltip (u8 ("L\xc3\xb6st eine einzelne B\xc3\xb6" "e aus (anschwellen, abklingen). Auch als Automationsparameter nutzbar."));
    autoGust.slider.setTooltip (u8 ("Zuf\xc3\xa4llige B\xc3\xb6" "en: wie oft und wie kr\xc3\xa4" "ftig. Ganz links aus. Gezielte B\xc3\xb6" "en kommen zus\xc3\xa4tzlich dazu."));
    shelter.slider.setTooltip (u8 ("Perspektive: 0 % drau\xc3\x9f" "en, ca. 50 % hinter Fenster oder Zeltwand, 100 % tief im Geb\xc3\xa4ude (Keller). Dazu tiefes Wellblech-Rappeln und Druck auf die W\xc3\xa4nde."));
    howl.slider.setTooltip (u8 ("Heulen und Pfeifen: 0 % reines Rauschen, 100 % ein deutlicher, wandernder Ton. Der Klang h\xc3\xa4ngt von ENVIRONMENT ab; bei hohem SHELTER auch das Pfeifen in Ritzen."));
    baseLevel.slider.setTooltip (u8 ("Lautst\xc3\xa4rke des dauerhaften Grundwinds (verhindert digitale Stille). Ganz links aus."));
    output.slider.setTooltip (u8 ("Master-Pegel des Windes. Das Eingangssignal bleibt unver\xc3\xa4ndert."));
    for (int i = 0; i < gale::kNumEnvs; ++i)
        envButtons[i].setTooltip (u8 (kEnvNote[i]));

    applyTheme();
    // zuletzt gespeicherte Fenstergroesse wiederherstellen (sonst 100 %)
    const int savedW = (int) processor.apvts.state.getProperty (kUiWProp, kBaseW);
    const int restoredW = juce::jlimit (kBaseW / 2, kBaseW * 3, savedW);
    setSize (restoredW, juce::roundToInt ((float) restoredW * (float) kBaseH / (float) kBaseW));
    sizeReady = true;
    startTimerHz (30);
}

GaleEditor::~GaleEditor() { stopTimer(); setLookAndFeel (nullptr); }

void GaleEditor::updateEnvButtons (int index)
{
    index = juce::jlimit (0, gale::kNumEnvs - 1, index);
    for (int i = 0; i < gale::kNumEnvs; ++i)
        envButtons[i].setToggleState (i == index, juce::dontSendNotification);
    envNote.setText (u8 (kEnvNote[index]), juce::dontSendNotification);
}

void GaleEditor::timerCallback()
{
    const float a = processor.activity.load();
    const float lit = a > 0.04f ? juce::jlimit (0.0f, 1.0f, 0.35f + a) : 0.0f;
    if (std::abs (lit - triggerButton.led) > 0.02f)
    {
        triggerButton.led = lit;
        triggerButton.repaint();
    }
}

void GaleEditor::applyTheme()
{
    look.setColour (juce::Slider::thumbColourId, theme.accent);
    look.setColour (juce::Slider::rotarySliderFillColourId, theme.accent);
    look.setColour (juce::Slider::rotarySliderOutlineColourId, theme.line);
    look.setColour (juce::Slider::trackColourId, theme.accent.withAlpha (0.75f));
    look.setColour (juce::Slider::backgroundColourId, theme.line);
    look.setColour (juce::Slider::textBoxTextColourId, theme.text);
    look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour (juce::ComboBox::backgroundColourId, theme.bg);
    look.setColour (juce::ComboBox::textColourId, theme.text);
    look.setColour (juce::ComboBox::arrowColourId, theme.text);
    look.setColour (juce::ComboBox::outlineColourId, theme.line);
    look.setColour (juce::PopupMenu::backgroundColourId, theme.panel);
    look.setColour (juce::PopupMenu::textColourId, theme.text);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accent);
    look.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    look.setColour (juce::Label::textColourId, theme.text);
    look.setColour (juce::TextButton::buttonColourId, theme.dark ? theme.panel : theme.bg);
    look.setColour (juce::TextButton::buttonOnColourId, theme.accent);
    look.setColour (juce::TextButton::textColourOffId, theme.text);
    look.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    envLabel.setColour (juce::Label::textColourId, theme.dim);
    envNote.setColour (juce::Label::textColourId, theme.dim);
    envNote.setFont (juce::FontOptions (11.0f));
    triggerNote.setColour (juce::Label::textColourId, theme.dim);
    triggerNote.setFont (juce::FontOptions (11.0f));
    themeButton.setButtonText (theme.dark ? "Look: Dunkel" : "Look: Hell");
    helpText.setAttributed (buildHelp (theme.text, theme.accent, theme.dim), theme.panel);
    helpScroll.fill = theme.panel;
    helpScroll.edge = theme.line;
    sendLookAndFeelChange();
    repaint();
}

void GaleEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text)
{
    // nur senkrecht ziehen (wie bei den anderen Plugins): hoch = mehr, runter = weniger
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setMouseDragSensitivity (180);   // Pixel fuer den ganzen Regelweg
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    content.addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    content.addAndMakeVisible (k.label);
    k.attachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void GaleEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    g.addTransform (juce::AffineTransform::scale (uiScale));
    const int W = kBaseW;

    // Kopfleiste: folgt dem Look (dunkel = dunkle Leiste + helles Logo, hell = helle Leiste + dunkles Logo)
    g.setColour (theme.panel);
    g.fillRect (0, 0, W, 64);
    g.setColour (theme.line);
    g.fillRect (0, 63, W, 1);
    g.setColour (theme.accent);
    g.fillRect (0, 0, 5, 64);

    if (logo.isValid())
    {
        g.setColour (theme.text);   // Logo ist eine Tinten-Maske und wird in der Textfarbe gezeichnet
        g.drawImage (logo, juce::Rectangle<float> (14.0f, 4.0f, 56.0f, 56.0f),
                     juce::RectanglePlacement::centred, true);
    }

    const float subX = 166.0f;
    g.setColour (theme.text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("GALE", 80, 8, 80, 30, juce::Justification::centredLeft);
    g.setColour (theme.accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("AUDIO DRAMA WIND & GUST SIMULATOR", (int) subX, 15, 340, 22, juce::Justification::centredLeft);
    g.setColour (theme.dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (u8 ("STROMSCHLAG AUDIO  \xc2\xb7  v" PLUGIN_VERSION_TEXT), 80, 36, 300, 16, juce::Justification::centredLeft);

    // Kleines Symbol: drei Windlinien mit Wirbel am Ende (einfarbig, folgt dem Look)
    {
        juce::Path lines;
        lines.startNewSubPath (0.0f, 6.0f);   lines.lineTo (15.0f, 6.0f);
        lines.cubicTo (21.0f, 6.0f, 23.5f, 1.0f, 19.0f, 1.0f);
        lines.startNewSubPath (0.0f, 12.5f);  lines.lineTo (24.0f, 12.5f);
        lines.cubicTo (30.5f, 12.5f, 33.0f, 19.5f, 27.5f, 19.5f);
        lines.startNewSubPath (4.0f, 19.0f);  lines.lineTo (14.0f, 19.0f);
        lines.cubicTo (18.0f, 19.0f, 19.0f, 24.0f, 15.5f, 24.0f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)),
                          "AUDIO DRAMA WIND & GUST SIMULATOR", 0.0f, 0.0f);
        const float subEnd = subX + ga.getBoundingBox (0, -1, true).getWidth();   // Ende des Untertitels
        const float helpX  = (float) (kBaseW - 170);                              // linke Kante des Look-Knopfs
        const float mid    = 0.5f * (subEnd + helpX);                             // Mitte dazwischen
        lines.applyTransform (juce::AffineTransform::scale (1.3f).translated (mid - 21.0f, 17.0f));
        g.setColour (theme.panel.interpolatedWith (theme.text, 0.30f));
        g.strokePath (lines, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Obere Karte: ENVIRONMENT | GUST | TRIGGER
    g.setColour (theme.panel);
    g.fillRoundedRectangle (12.0f, 76.0f, (float) W - 24.0f, 150.0f, 8.0f);
    g.setColour (theme.line);
    g.drawRoundedRectangle (12.5f, 76.5f, (float) W - 25.0f, 149.0f, 8.0f, 1.0f);
    g.fillRect (264.0f, 88.0f, 1.0f, 126.0f);
    g.fillRect (416.0f, 88.0f, 1.0f, 126.0f);

    // Untere Kaesten: CHARACTER | WIND | MASTER
    struct Box { float x, w; const char* title; const char* note; };
    const Box boxes[] = { { 12.0f, 224.0f, "CHARACTER", "Perspektive" },
                          { 244.0f, 224.0f, "WIND", "" },
                          { 476.0f, (float) W - 488.0f, "MASTER", "" } };
    for (const auto& b : boxes)
    {
        g.setColour (theme.panel);
        g.fillRoundedRectangle (b.x, 236.0f, b.w, 172.0f, 8.0f);
        g.setColour (theme.line);
        g.drawRoundedRectangle (b.x + 0.5f, 236.5f, b.w - 1.0f, 171.0f, 8.0f, 1.0f);
        g.setColour (theme.accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (b.title, (int) b.x + 12, 242, (int) b.w - 24, 16, juce::Justification::centredLeft);
        g.setColour (theme.dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (u8 (b.note), (int) b.x + 12, 242, (int) b.w - 24, 16, juce::Justification::centredRight);
    }
}

void GaleEditor::resized()
{
    if (sizeReady)
    {
        processor.apvts.state.setProperty (kUiWProp, getWidth(), nullptr);
        processor.apvts.state.setProperty (kUiHProp, getHeight(), nullptr);
    }
    uiScale = std::min ((float) getWidth() / kBaseW, (float) getHeight() / kBaseH);
    content.setTransform (juce::AffineTransform::scale (uiScale));
    content.setBounds (0, 0, kBaseW, kBaseH);
    repaint();

    const int w = kBaseW;
    // Look links, Hilfe ganz rechts
    themeButton.setBounds (w - 170, 18, 112, 28);
    helpButton.setBounds (w - 48, 18, 32, 28);
    helpScroll.setBounds (12, 76, w - 24, 332);   // deckt Karte und Kaesten genau ab (y 76 bis 408)
    helpText.setWidth (helpScroll.getMaximumVisibleWidth());

    // Obere Karte
    envLabel.setBounds (24, 84, 200, 18);
    for (int i = 0; i < gale::kNumEnvs; ++i)
        envButtons[i].setBounds (24 + (i % 2) * 120, 108 + (i / 2) * 40, 112, 32);
    envNote.setBounds (24, 192, 232, 30);

    gust.label.setBounds (270, 84, 140, 18);
    gust.slider.setBounds (278, 104, 124, 118);

    blowButton.setBounds (432, 90, 224, 44);
    triggerButton.setBounds (432, 142, 224, 44);
    triggerNote.setBounds (432, 190, 224, 30);

    auto placeKnob = [] (Knob& k, int x, int y, int cw)
    {
        k.label.setBounds (x, y, cw, 18);
        k.slider.setBounds (x + 10, y + 20, cw - 20, 118);
    };
    // CHARACTER: SHELTER, HOWL
    placeKnob (shelter, 16, 264, 108);
    placeKnob (howl, 124, 264, 108);
    // WIND: BASE LEVEL, AUTO GUST
    placeKnob (baseLevel, 248, 264, 108);
    placeKnob (autoGust, 356, 264, 108);
    // MASTER: OUTPUT
    placeKnob (output, 484, 264, w - 12 - 6 - 484);
    helpScroll.toFront (false);
}
