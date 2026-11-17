// Regressionstests der GALE-Klang-Engine (ohne JUCE). Pruefen Verhalten, nicht Klang: das entscheidet das Ohr.
#include "GaleDSP.h"
#include "TestSupport.h"
#include <limits>
#include <vector>

using gale::Params;

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) (b - a) + 1e-20);
}

static Params baseParams()
{
    Params p; p.gust = 0.4f; p.autoGust = 0.5f; p.howl = 0.7f; p.shelter = 0.4f;
    return p;
}

// Rendert n Samples in Bloecken der Groesse "block" (0 = alles auf einmal); Trigger nur im allerersten Aufruf
static void render (double fs, Params p, int n, int block, std::vector<float>& l, std::vector<float>& r, bool trigger = true)
{
    gale::Engine e; e.prepare (fs);
    l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f);
    const int step = block > 0 ? block : n;
    for (int at = 0; at < n; at += step)
    {
        const int m = std::min (step, n - at);
        p.trigger = trigger && at == 0;
        NoAllocations guard;
        e.process (l.data() + at, r.data() + at, m, p);
    }
}

int main()
{
    NoDenormals denormals;

    // 1) Alle Abtastraten und Umgebungen: endlich, begrenzt, und unabhaengig von der Blockaufteilung (bit-genau)
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int env = 0; env < gale::kNumEnvs; ++env)
        {
            Params p = baseParams(); p.env = env;
            std::vector<float> a, b, c, d;
            const int n = (int) (fs * 0.6) + 13;
            render (fs, p, n, 0, a, d);
            render (fs, p, n, 137, b, d);
            render (fs, p, n, 1, c, d);
            CHECK (a == b);
            CHECK (a == c);
            for (float x : a) CHECK (std::isfinite (x) && std::abs (x) <= 1.05f);
        }

    // 2) Ungueltige Werte (NaN, unendlich, riesig) duerfen nichts kaputt machen
    {
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        Params p;
        p.gust = nan; p.autoGust = inf; p.shelter = -inf; p.howl = nan; p.baseDb = nan; p.outDb = inf;
        p.gustShape = nan; p.sweep = inf; p.rattle = nan; p.env = 999;
        std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
        gale::Engine e; e.prepare (48000);
        e.process (l.data(), r.data(), 4800, p);
        for (size_t i = 0; i < l.size(); ++i) CHECK (std::isfinite (l[i]) && std::isfinite (r[i]) && std::abs (l[i]) <= 1.05f);
        float xl = nan, xr = nan;   // auch ein ungueltiger Eingang darf nur ihn selbst treffen
        e.process (&xl, &xr, 1, Params());
        gale::Engine e2; e2.prepare (std::numeric_limits<double>::quiet_NaN());   // ungueltige Abtastrate -> 48 kHz
        e2.process (l.data(), r.data(), 64, Params());
    }

    // 3) Aus: ohne Grundwind und ohne Boeen kommt nichts heraus; der Eingang laeuft unveraendert durch
    {
        Params p; p.gust = 0.0f; p.autoGust = 0.0f; p.baseDb = -80.0f; p.shelter = 0.0f;
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.25f * (float) std::sin (0.05 * (double) i);
        const auto ref = l;
        gale::Engine e; e.prepare (48000);
        e.process (l.data(), r.data(), 48000, p);
        CHECK (l == ref);
        CHECK (r == ref);
    }

    // 4) Standardeinstellung: Grundwind liegt im sinnvollen Bereich (etwa -30 LUFS, hier als RMS -45 bis -25 dB)
    {
        std::vector<float> l, r;
        render (48000, Params(), 48000 * 10, 512, l, r, false);
        const double db = rmsDb (l, 48000, l.size());
        CHECK (db > -45.0 && db < -25.0);
    }

    // 5) TRIGGER GUST: die Boee hebt den Pegel deutlich (>= 8 dB) und klingt wieder ab
    {
        Params p; p.gust = 0.10f; p.autoGust = 0.0f; p.baseDb = -40.0f;
        std::vector<float> l, r;
        gale::Engine e; e.prepare (48000);
        l.assign (48000 * 12, 0.0f); r = l;
        for (int at = 0; at < (int) l.size(); at += 256)
        {
            p.trigger = at >= 48000 * 4 && at < 48000 * 4 + 6144;   // Boee bei 4 s
            e.process (l.data() + at, r.data() + at, 256, p);
        }
        const double before = rmsDb (l, 48000 * 2, 48000 * 4);
        const double peak = rmsDb (l, 48000 * 4 + 40000, 48000 * 5 + 20000);
        const double after = rmsDb (l, 48000 * 11, 48000 * 12);
        CHECK (peak > before + 8.0);
        CHECK (after < peak - 6.0);
    }

    // 6) Stereo und Mono-Kompatibilitaet: L/R unabhaengig, die Summe loescht sich nicht aus und ist nicht stark ueberhoeht
    for (int env = 0; env < gale::kNumEnvs; ++env)
        for (float sh : { 0.0f, 0.6f, 1.0f })
        {
            Params p = baseParams(); p.env = env; p.shelter = sh; p.gust = 0.6f;
            std::vector<float> l, r;
            render (48000, p, 48000 * 8, 480, l, r);
            double ll = 0, rr = 0, lr = 0, ss = 0;
            for (size_t i = 0; i < l.size(); ++i) { ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; lr += (double) l[i] * r[i]; ss += (double) (l[i] + r[i]) * (l[i] + r[i]); }
            const double corr = lr / std::sqrt (ll * rr);
            const double sumDb = 10.0 * std::log10 (ss / (ll + rr));   // 0 dB = unabhaengig, +3 dB = identisch, -inf = Ausloeschung
            CHECK (corr > -0.2 && corr < 0.6);
            CHECK (sumDb > -1.0 && sumDb < 2.5);
        }

    // 7) SHELTER: drinnen sind die Hoehen deutlich leiser als draussen (einfacher Test ueber die Differenz zweier Spektren)
    {
        auto highEnergy = [] (float sh)
        {
            Params p; p.gust = 0.7f; p.autoGust = 0.0f; p.shelter = sh; p.howl = 0.0f;
            std::vector<float> l, r;
            render (48000, p, 48000 * 4, 256, l, r, false);
            // einfacher Hochpass (Differenz zweiter Ordnung) als Mass fuer Hoehen
            double s = 0;
            for (size_t i = 2 * 48000; i + 2 < l.size(); ++i) { const double d = (double) l[i] - 2.0 * l[i - 1] + l[i - 2]; s += d * d; }
            return 10.0 * std::log10 (s + 1e-20);
        };
        CHECK (highEnergy (1.0f) < highEnergy (0.0f) - 20.0);
        CHECK (highEnergy (0.5f) < highEnergy (0.0f) - 6.0);
    }

    // 8) AUTO GUST: ohne Zufall bleibt der Pegel ruhig, mit Zufall schwankt er hoerbar
    {
        auto spread = [] (float autoG)
        {
            Params p; p.gust = 0.1f; p.autoGust = autoG; p.baseDb = -50.0f;
            std::vector<float> l, r;
            render (48000, p, 48000 * 60, 480, l, r, false);
            double lo = 1e9, hi = -1e9;
            for (int s = 2; s < 58; ++s)
            {
                const double d = rmsDb (l, (size_t) s * 48000, (size_t) (s + 1) * 48000);
                lo = std::min (lo, d); hi = std::max (hi, d);
            }
            return hi - lo;
        };
        CHECK (spread (1.0f) > 8.0);
        CHECK (spread (0.0f) < 6.0);
    }

    // 9) Umgebungswechsel und Shelter-Sprung (0 <-> 50 %) mitten im Lauf: keine Spruenge ueber 12 dB zwischen aufeinanderfolgenden 10-ms-Bloecken
    {
        gale::Engine e; e.prepare (48000);
        Params p = baseParams();
        std::vector<float> l (48000 * 4, 0.0f), r = l;
        for (int at = 0; at < (int) l.size(); at += 64)
        {
            p.env = (at / 24000) & 3;   // alle 0,5 s anderer Charakter
            p.shelter = (at / 36000) % 2 ? 0.5f : 0.0f;
            e.process (l.data() + at, r.data() + at, 64, p);
        }
        double prev = 0;
        for (size_t at = 4800; at + 480 <= l.size(); at += 480)
        {
            const double d = rmsDb (l, at, at + 480);
            if (prev != 0) CHECK (std::abs (d - prev) < 12.0);
            prev = d;
        }
    }

    // 10) Reset: nach reset() entsteht exakt dasselbe wie beim ersten Mal (gleicher Zufall bei jedem Abspielen)
    {
        std::vector<float> l1, r1, l2 (48000, 0.0f), r2 (48000, 0.0f);
        Params p = baseParams();
        render (48000, p, 48000, 100, l1, r1);
        gale::Engine e; e.prepare (48000);
        std::vector<float> junk (5000, 0.0f), junk2 = junk;
        e.process (junk.data(), junk2.data(), 5000, p);
        e.reset();
        p.trigger = true;
        e.process (l2.data(), r2.data(), 100, p);
        p.trigger = false;
        e.process (l2.data() + 100, r2.data() + 100, 48000 - 100, p);
        CHECK (l1 == l2);
    }

    // 11) Staerkste Einstellung mit Ausgang +24 dB: weiches Begrenzen haelt alles unter 1,0
    {
        Params p; p.gust = 1.0f; p.autoGust = 1.0f; p.shelter = 0.6f; p.howl = 1.0f; p.outDb = 24.0f; p.baseDb = 6.0f;
        std::vector<float> l, r;
        render (48000, p, 48000 * 6, 480, l, r);
        for (float x : l) CHECK (std::abs (x) <= 1.001f);
    }

    // 12) BLOW: aus = nach dem Ausklingen Stille (Eingang laeuft weiter), Ein-/Ausblenden ohne Spruenge
    {
        gale::Engine e; e.prepare (48000);
        Params p = baseParams(); p.gust = 0.6f; p.shelter = 0.6f; p.autoGust = 1.0f;
        const int n = 48000 * 10;
        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        for (int at = 0; at < n; at += 128)
        {
            p.blow = ! (at >= 48000 * 2 && at < 48000 * 7);   // 2 s an, 5 s aus, dann wieder an
            e.process (l.data() + at, r.data() + at, std::min (128, n - at), p);
        }
        const double on = rmsDb (l, 48000, 96000);
        CHECK (on > -50.0);
        CHECK (rmsDb (l, 48000 * 5, 48000 * 7) < -120.0);       // nach 2,5 s Ausklingen still
        CHECK (rmsDb (l, 48000 * 2 + 4800, 48000 * 2 + 9600) > on - 6.0);   // klingt weich aus, nicht abrupt
        CHECK (rmsDb (l, 48000 * 7, 48000 * 7 + 2400) < on - 12.0);           // blendet weich ein
        CHECK (rmsDb (l, 48000 * 9, 48000 * 10) > on - 10.0);                 // ist wieder da
        float maxStep = 0;
        for (size_t i = 48000 * 2 - 10; i < (size_t) 48000 * 8; ++i) maxStep = std::max (maxStep, std::abs (l[i] - l[i - 1]));
        CHECK (maxStep < 0.2f);
    }

    std::puts ("Gale: rates, environments, block partition, invalid values, passthrough, trigger, stereo/mono, shelter, auto gust, reset, limiter, blow and allocation checks passed");
}
