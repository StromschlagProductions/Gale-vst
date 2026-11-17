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
Frequenz-/Q-/Sampleratenwerten dieselben Koeffizienten weiter. Glättungskonstanten
werden bei Prepare berechnet statt alle 16 Samples. Filterzustände, Rauschfolgen,
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

`compare_reference.py` vergleicht jedes Sample bei 22.050, 44.100, 48.000, 96.000 und
192.000 Hz und allen vier Umgebungen über einen vollständigen BLOW-Aus-/Ein-Zyklus.
Gemessen: **19.303.200 Kanal-Samples bitidentisch**. Das gilt für diese Testverläufe,
nicht als Beweis für alle möglichen Signale oder einen subjektiven Hörtest.
Die reparierten Triggerfälle sollen gegenüber 1.1 ausdrücklich anders reagieren.

## Performance und Grenzen

Linux x86_64, GCC 14.2, `-O3`, FTZ/DAZ, fünf Läufe je Version in wechselnder
Reihenfolge; Median der Prozess-CPU-Zeit für je 512.000 Samples bei 48 kHz.
Shelter 60 %, sonst Standardwerte, Eingang null. Rüst-/Eingangspufferarbeit im
Benchmark ist mitgemessen; weitere Compilerarbeit lief parallel im Container.
Kleine Differenzen (insbesondere rund 1 %) sind nicht belastbar interpretierbar.

| Umgebung | BLOW | 1.1 ms CPU | 1.2 ms CPU | Änderung |
|---|---|---:|---:|---:|
| Open Land | aus | 115.54 | 104.86 | -9.2 % |
| Open Land | an | 112.61 | 105.49 | -6.3 % |
| Trees | aus | 170.08 | 161.68 | -4.9 % |
| Trees | an | 170.22 | 159.97 | -6.0 % |
| Rocks | aus | 113.34 | 106.34 | -6.2 % |
| Rocks | an | 113.92 | 104.35 | -8.4 % |
| Cracks | aus | 97.71 | 96.64 | -1.1 % |
| Cracks | an | 98.19 | 91.34 | -7.0 % |

Aktiver Wind benötigt in diesen Messungen etwa 6–8 % weniger CPU. Es gibt keinen
Nachweis geringerer maximaler Callback-Zeiten im Vollplugin. BLOW aus ist weiterhin
kein CPU-Schlafmodus. Rohwerte: `docs/benchmark-results.json`.

`tools/benchmark.cpp`
lässt sich unverändert mit `-O3 -std=c++17` und `-DHEADER=\"/absoluter/Pfad/GaleDSP.h\"`
gegen beide Header kompilieren. Es misst CPU-Zeit der Engine (nicht die DAW-CPU-Anzeige).

Manuell in der Ziel-DAW prüfen: Trigger aufnehmen/automatisieren, Mono-/Stereo-Spuren,
BLOW über längere Szenen aus-/einschalten, alte Projekte laden und mehrere Fenster
unabhängig bedienen. Native Fensterinteraktion und DAW-spezifische Abläufe ersetzt
kein Test ohne Host. Es gibt weiterhin keine samplegenauen Triggerzeitstempel innerhalb
eines Audioblocks; Ereignisse werden an dessen Anfang übernommen.
