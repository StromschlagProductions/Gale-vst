# Gale 1.2 – technische Qualitätsverbesserung

Basis: Gale 1.1, Commit `8019da2b079c3c460910dcb8c2744b82d884d546`.
Keine Änderung an Plugin-/Herstellerkennungen, Parameter-IDs, Wertebereichen oder
bestehenden XML-States (`GALE`). Latenz bleibt 0 Samples.

## Behobene Fehler

- Mono verarbeitet große Hostblöcke in vorreservierten Abschnitten. Der alte Code
  setzte den Trigger nach dem ersten Abschnitt auf false. Bei gehaltenem Trigger
  wurde dadurch jeder folgende Hostblock als erneute steigende Flanke erkannt.
- Ein boolesches Trigger-Postfach konnte mehrere Klicks zusammenfassen und aufeinander-
  folgende kurze Klicks durch die Flankenerkennung verlieren. Der neue atomare
  32-Bit-Ereigniszähler zählt echte steigende Flanken. Der Audiothread übernimmt die
  Differenz und übergibt Ereignisse nur am Anfang des Hostblocks an die Engine.
  Maximal acht Ereignisse pro Callback werden verarbeitet, passend zu acht Stimmen;
  mehr zwischen zwei Callbacks werden bewusst begrenzt. Keine Allokation und kein
  unbeschränktes Abarbeiten einer Warteschlange. Ein gehaltener Pegel ist ein Ereignis.
- Reset/Prepare übernehmen den aktuellen Zählerstand: alte Klicks starten nach dem
  Reset keine neue Böe. Ein weiterhin gehaltener Taster muss erst losgelassen werden.
- Fensterdaten greifen nicht mehr direkt auf den bei Restore austauschbaren APVTS-
  ValueTree zu. Theme und Größe sind atomare Metadaten; beim Speichern werden sie in
  eine State-Kopie geschrieben. Die Oberfläche übernimmt Restore im GUI-Timer.
- Preset-Index ist atomar und wird als zusätzliche State-Eigenschaft gespeichert.
  Alte States ohne diese Eigenschaft bleiben ladbar (Index 0, Parameter unverändert).

- Beim State-Restore werden BLOW/Trigger zusätzlich auf kanonische 0/1-Werte
  zurückgeführt. JUCE 8 kann sonst einen vom Host gesetzten Zwischenwert behalten,
  obwohl der APVTS-Rohwert bereits auf denselben booleschen Zustand gerundet war.
  Ein gezielter Test prüft 0.839735→1 und 0.14→0.

## Klang und BLOW

Das BLOW-Verhalten wurde bewusst unverändert gelassen: laufende Engine ca. 2,5 s
weich ausblenden, ca. 1,5 s einblenden. Der anfängliche BLOW-Zustand bei Prepare wird
wie bisher direkt übernommen. Keine neue Verzögerung, kein Reset beim BLOW-Wechsel.

Ein vollständiger CPU-Schlafmodus wurde verworfen: Er würde die internen Rauschquellen,
Filterhistorien und Rappel-Ereignisse während der Stille anhalten oder zurücksetzen.
Das könnte beim Einschalten einen anderen Verlauf erzeugen. Die Simulation bleibt
deshalb aktiv; auch im stummen Zustand sparen nur nachweislich redundante Berechnungen
CPU. Keine verkürzten Fahnen, keine eingefrorenen/aufgestauten Rappel-Ereignisse.

Optimiert wurden zustandslose Berechnungen: `Filt::set` verwendet bei exakt gleichen
Frequenz-/Q-/Sampleratenwerten dieselben Koeffizienten weiter. Die ursprüngliche Berechnung der
Glättungskonstanten bleibt erhalten (eine Vorberechnung erzeugte bei einem anderen
Compiler minimale Abweichungen und wurde verworfen). Filterzustände, Rauschfolgen,
Steuerraster, BLOW-Kurve und Klangparameter bleiben erhalten.

## Threads und Echtzeit

Audio: Parameter atomar lesen, eigene Engine-/Pufferzustände bearbeiten, Triggerzähler
konsumieren. Parameter-Listener: ausschließlich atomare Flankenerfassung und Zähler-
inkrement. GUI: Attachments und atomare Metadaten, keine direkten State-Zugriffe.
State-Callbacks: JUCE `copyState`/`replaceState`; diese gehören nicht in den Audiocallback.
Prepare, Reset und Zerstörung benötigen den üblichen Host-Lebenszyklus ohne parallel
laufende Verarbeitung derselben Engine. Mehrere Instanzen besitzen getrennte Zustände.

Der globale C++-Allokationszähler im Test überwacht `new`/`new[]` auf dem geprüften
Thread, keine beliebigen externen `malloc`- oder Betriebssystemaufrufe.

## Reproduzierbare Prüfung

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
# Vergleich mit dem originalen 1.1-Header, benötigt dessen Git-Historie und g++:
python3 tools/compare_reference.py
# Nur DSP ohne JUCE:
cmake -S . -B build-dsp -DAUDIO_DSP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure
```

Die neuen Prozessor-Regressionen vergleichen das tatsächliche Mono-/Stereo-Ausgangs-
signal mit einer Engine, die genau die erwartete Anzahl Ereignisse erhält: gehaltene
Taste, benachbarte kurze Klicks, Klickfolgen zwischen Callbacks, Reset und Prepare,
64/8192/32769 Samples bei Prepare mit 64 Samples. Zusätzlich: Fenster-Lebensdauer,
State-Metadaten, parallele Instanzen, gleichzeitiges Restore und Presetwechsel.
Bestehende DSP-Tests prüfen u. a. fünf Abtastraten, Blockaufteilung, BLOW und Ausklingen.

`compare_reference.py` vergleicht mit deaktivierter FMA-Kontraktion (`-ffp-contract=off`)
jedes Sample bei 22.050, 44.100, 48.000, 96.000 und
192.000 Hz und allen vier Umgebungen über einen vollständigen BLOW-Aus-/Ein-Zyklus.
Gemessen: **19.303.200 Kanal-Samples bitidentisch**. Das gilt für diese Testverläufe,
nicht als Beweis für alle möglichen Signale oder einen subjektiven Hörtest.
Andere Compiler-/FMA-Einstellungen können trotz gleicher Formeln minimale
Rundungsunterschiede erzeugen; bitidentische Ausgabe wird dafür nicht versprochen.
Die reparierten Triggerfälle sollen gegenüber 1.1 ausdrücklich anders reagieren.

## Performance und Grenzen

Linux x86_64, GCC 14.2, `-O3`, FTZ/DAZ, fünf Läufe je Version in wechselnder
Reihenfolge; Median der Prozess-CPU-Zeit für je 2.560.000 Samples bei 48 kHz.
Shelter 60 %, sonst Standardwerte, Eingang null. Rüst-/Eingangspufferarbeit im
Benchmark ist mitgemessen; Zu Beginn lief der lokale pluginval-Lauf noch parallel im Container.
Kleine Differenzen (insbesondere rund 1 %) sind nicht belastbar interpretierbar.

| Umgebung | BLOW | 1.1 ms CPU | 1.2 ms CPU | Änderung |
|---|---|---:|---:|---:|
| Open Land | aus | 533.70 | 502.12 | -5.9 % |
| Open Land | an | 539.24 | 491.76 | -8.8 % |
| Trees | aus | 780.70 | 770.36 | -1.3 % |
| Trees | an | 800.09 | 775.81 | -3.0 % |
| Rocks | aus | 532.79 | 506.42 | -4.9 % |
| Rocks | an | 531.45 | 503.60 | -5.2 % |
| Cracks | aus | 483.06 | 446.11 | -7.6 % |
| Cracks | an | 472.24 | 440.96 | -6.6 % |

Aktiver Wind benötigt in diesen Messungen je nach Umgebung etwa 3–9 % weniger CPU.
Es gibt keinen Nachweis geringerer maximaler Callback-Zeiten im Vollplugin.
BLOW aus ist weiterhin kein CPU-Schlafmodus. Rohwerte: `docs/benchmark-results.json`.

`tools/benchmark.cpp`
lässt sich unverändert mit `-O3 -std=c++17` und `-DHEADER=\"/absoluter/Pfad/GaleDSP.h\"`
gegen beide Header kompilieren. Es misst CPU-Zeit der Engine (nicht die DAW-CPU-Anzeige).

Manuell in der Ziel-DAW prüfen: Trigger aufnehmen/automatisieren, Mono-/Stereo-Spuren,
BLOW über längere Szenen aus-/einschalten, alte Projekte laden und mehrere Fenster
unabhängig bedienen. Native Fensterinteraktion und DAW-spezifische Abläufe ersetzt
kein Test ohne Host. Es gibt weiterhin keine samplegenauen Triggerzeitstempel innerhalb
eines Audioblocks; Ereignisse werden an dessen Anfang übernommen.
