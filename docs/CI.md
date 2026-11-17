# CI-Nutzung (GitHub Actions)

Alle Plugin-Repos sind öffentlich, die Actions-Minuten spielen keine Rolle. Deshalb laufen die Prüfungen bei jeder Änderung automatisch. Das Veröffentlichen bleibt eine bewusste Freigabe.

| Workflow | Runner | Läuft bei | Inhalt |
|---|---|---|---|
| `build.yml` Windows-VST3 bauen | Windows | Push auf `main`, Pull Request, **Run workflow** von Hand (alles außer reinen Doku-Änderungen) | Hersteller-Prüfung, Build, Regressionstests (DSP und Prozessor), pluginval Strictness 8, ZIP als Artefakt |
| `tests.yml` DSP and memory safety | Linux | Push auf `main` und Pull Request bei Änderungen an `Source/`, `Tests/`, `tools/`, `CMakeLists.txt`, `VERSION` | Klang-Engine mit AddressSanitizer und UndefinedBehaviorSanitizer (ohne JUCE, daher schnell) |

## Ablauf

1. Lokal oder im Branch entwickeln. Die Versionsnummer in `VERSION` bei jeder Änderung hochzählen.
2. Nach dem Merge in `main` baut und prüft GitHub automatisch; das Plugin liegt als Artefakt („Gale-VST3-Windows“) zum Herunterladen bereit.
3. Zur Veröffentlichung: GitHub > Actions > „Windows-VST3 bauen“ > „Run workflow“ (Branch `main`) mit Haken **publish**. Das erstellt das Release `v<VERSION>` mit `Gale-v<VERSION>-Windows-VST3.zip` und setzt den Download-Link der README („latest“) auf die neue Version. Dafür muss `docs/releases/<VERSION>.md` (Release-Hinweise) vorhanden sein; fehlt sie, bricht der Lauf nach Sekunden ab.

Es wird nie automatisch ein Release erstellt.

## Lokal bauen und prüfen

```
# nur Klang-Engine und Tests (ohne JUCE, ueberall)
cmake -S . -B build/dsp -DAUDIO_DSP_ONLY=ON && cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure

# Hoerbeispiele rendern und messen (Lautheit, Spitze, Stereo/Mono, Spektrogramme)
cmake --build build/dsp --target RenderDemo && build/dsp/RenderDemo hoerbeispiele/
python3 tools/analyze.py hoerbeispiele/ --plots hoerbeispiele/plots

# komplettes Plugin
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Verbindliches öffentliches Versionsschema: `docs/VERSIONING.md` (ohne Build-Suffix).
