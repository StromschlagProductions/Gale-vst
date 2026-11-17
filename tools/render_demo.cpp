// render_demo.cpp - rendert die Hoerbeispiele von GALE aus der echten Engine (Source/GaleDSP.h) als WAV.
// Aufruf: render_demo <Zielordner> [Abtastrate]
// Dieselben Boeen-Zeitpunkte in allen Varianten einer Sturmszene (feste Zufallssaat), damit man draussen/drinnen vergleichen kann.
#include "../Source/GaleDSP.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
struct Scene
{
    std::string name;
    float seconds;
    std::function<gale::Params (float)> at;     // Parameter zur Zeit t
    std::vector<float> triggers;                // Zeitpunkte gezielter Boeen (TRIGGER GUST)
};

float ramp (float t, float t0, float t1, float a, float b)
{
    if (t <= t0) return a;
    if (t >= t1) return b;
    return a + (b - a) * (t - t0) / (t1 - t0);
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::perror (path.c_str()); return; }
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 2 * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (3); w16 (2); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&l[i], 4, 1, f); std::fwrite (&r[i], 4, 1, f); }
    std::fclose (f);
}

gale::Params make (int env, float gust, float autoG, float shelter, float howl)
{
    gale::Params p;
    p.env = env; p.gust = gust; p.autoGust = autoG; p.shelter = shelter; p.howl = howl;
    return p;
}

std::vector<Scene> scenes()
{
    using gale::Params;
    std::vector<Scene> s;
    // Die Sturmszene (Hurrikan ueber dem Blechlabor): Boeen auf Dialogzeilen bei 5, 14, 23, 31 s
    const std::vector<float> stormTrig = { 5.0f, 14.0f, 23.0f, 31.0f };
    auto storm = [] (float shelter) { return [shelter] (float) { return make (0, 0.55f, 0.7f, shelter, 0.6f); }; };

    s.push_back ({ "01_Outback_Stillness", 32.0f, [] (float) { return make (0, 0.12f, 0.15f, 0.0f, 0.2f); }, {} });
    s.push_back ({ "02_Sudden_Desert_Gust", 32.0f, [] (float) { return make (0, 0.12f, 0.0f, 0.0f, 0.5f); }, { 5.0f, 14.0f, 23.0f } });
    s.push_back ({ "03_Howling_Canyon", 36.0f, [] (float t) { return make (2, ramp (t, 0, 30, 0.3f, 0.8f), 0.5f, 0.0f, 0.8f); }, { 8.0f, 18.0f, 27.0f } });
    s.push_back ({ "04_Storm_Outside_Cabin_Indoor", 40.0f, [] (float) { return make (1, 0.55f, 0.6f, 0.55f, 0.5f); }, { 6.0f, 20.0f } });
    s.push_back ({ "05_Drafty_Doorway_Whistle", 32.0f, [] (float t) { return make (3, ramp (t, 0, 30, 0.35f, 0.7f), 0.0f, 0.3f, 0.8f); }, { 6.0f, 15.0f, 24.0f } });
    s.push_back ({ "06_Approaching_Tempest", 48.0f, [] (float t) { return make (0, ramp (t, 0, 44, 0.1f, 0.9f), ramp (t, 0, 44, 0.2f, 0.9f), 0.0f, ramp (t, 0, 44, 0.3f, 0.8f)); }, {} });
    s.push_back ({ "07a_Sturm_Blechlabor_draussen", 40.0f, storm (0.0f), stormTrig });
    s.push_back ({ "07b_Sturm_Blechlabor_drinnen", 40.0f, storm (0.62f), stormTrig });
    s.push_back ({ "07c_Sturm_Uebergang_draussen_nach_drinnen", 40.0f,
                   [] (float t) { return make (0, 0.55f, 0.7f, ramp (t, 6.0f, 32.0f, 0.0f, 1.0f), 0.6f); }, stormTrig });
    // Umgebungen im Vergleich: je 10 s, gleiche Staerke, Boeen alle 3 s
    s.push_back ({ "08_Umgebungen_Open_Trees_Rocks_Cracks", 40.0f,
                   [] (float t) { return make ((int) std::min (3.0f, t / 10.0f), 0.5f, 0.0f, 0.0f, 0.5f); },
                   { 2.0f, 5.0f, 8.0f, 12.0f, 15.0f, 18.0f, 22.0f, 25.0f, 28.0f, 32.0f, 35.0f, 38.0f } });
    return s;
}
}   // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "Aufruf: %s <Zielordner> [Abtastrate]\n", argv[0]); return 1; }
    const std::string dir = argv[1];
    const int fs = argc > 2 ? std::atoi (argv[2]) : 48000;

    std::vector<Scene> list = scenes();
    if (argc > 3 && std::string (argv[3]) == "calib")   // Pegel-Messung: jede Umgebung bei festen Staerken
    {
        list.clear();
        for (int e = 0; e < 4; ++e)
            for (float g : { 0.0f, 0.15f, 0.3f, 0.5f, 0.8f, 1.0f })
                for (float h : { 0.0f, 0.5f })
                {
                    if (g == 0.0f && h > 0) continue;
                    const std::string nm = "calib_e" + std::to_string (e) + "_g" + std::to_string ((int) (g * 100)) + "_h" + std::to_string ((int) (h * 100));
                    list.push_back ({ nm, 12.0f, [=] (float) { return make (e, g, 0.0f, 0.0f, h); }, {} });
                }
    }
    for (const auto& sc : list)
    {
        gale::Engine e; e.prepare (fs);
        const int n = (int) (sc.seconds * (float) fs);
        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        constexpr int block = 64;
        for (int i = 0; i < n; i += block)
        {
            const int m = std::min (block, n - i);
            const float t = (float) i / (float) fs;
            gale::Params p = sc.at (t);
            for (float tr : sc.triggers) if (t >= tr && t < tr + 0.12f) p.trigger = true;
            e.process (l.data() + i, r.data() + i, m, p);
        }
        writeWav (dir + "/" + sc.name + ".wav", l, r, fs);
        std::printf ("%s  (%.0f s)\n", sc.name.c_str(), sc.seconds);
    }
    return 0;
}
