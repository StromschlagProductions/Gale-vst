// GaleDSP.h - Klang-Engine fuer "Gale" (Stromschlag Audio)
// Reines C++ ohne JUCE, damit die Engine unabhaengig vom Plugin getestet und zu Hoerbeispielen gerendert werden kann.
//
// Samplefreier, prozeduraler Wind. Alles entsteht in Echtzeit aus Rauschen und Filtern:
//
//   Rauschen (links/rechts getrennt)  ->  Umgebungs-Stimme (Open / Trees / Rocks / Cracks)
//        zwei Schichten: Grundwind (BASE LEVEL) + Boeen-Schicht (GUST), dazu Heulen (HOWL)
//   ->  Shelter (Tiefpass, Wand-Wummern, Sub-Grollen, Ritzen-Pfeifen, Blech-Rappeln)  ->  Ausgang
//
// Stereo und Bewegung: Links und Rechts haben eigene, unabhaengige Rauschquellen. Die Boeen laufen dort mit
// zeitversetzten STEUERKURVEN (nie mit verzoegertem Audio). Darum ist die Summe aus L und R (Mono) unkritisch:
// keine Phasenausloeschungen, kein Kammfilter.
//
// Block-Unabhaengigkeit: Alle Steuerwerte werden in festen 16-Sample-Schritten gerechnet, der Zaehler gehoert zur
// Engine. Ergebnis ist bit-genau gleich, egal wie der Host den Strom in Bloecke teilt.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gale
{
constexpr float kPi = 3.14159265358979f;
constexpr int kCtl = 16;               // Steuerwerte alle 16 Samples
constexpr int kNumEnvs = 4;
constexpr int kMaxGusts = 8;
constexpr int kMaxHits = 96;

inline float finiteClamp (float x, float lo, float hi, float fallback) { return std::isfinite (x) ? std::clamp (x, lo, hi) : fallback; }
inline double validRate (double x) { return std::isfinite (x) && x >= 8000 && x <= 384000 ? x : 48000; }
inline float dbToLin (float db) { return db <= -79.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float logLerp (float a, float b, float t) { return a * std::pow (b / a, t); }
inline float smoothstep (float a, float b, float x)
{
    const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Weiches Begrenzen nur ueber 0,8 (darunter unveraendert), schuetzt vor Uebersteuern bei Extremwerten
inline float softClip (float x)
{
    const float a = std::fabs (x);
    if (a < 0.8f) return x;
    const float s = x < 0.0f ? -1.0f : 1.0f;
    return s * (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f));
}

//==============================================================================
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                             // [-1,1)
    float white() { return bi() * 1.7320508f; }                            // Effektivwert 1
};

// Zustandsvariablen-Filter (TPT). Die Ausgaenge sind leistungsnormiert: weisses Rauschen mit Effektivwert 1 am Eingang
// ergibt auch am Ausgang etwa Effektivwert 1, egal wo die Frequenz liegt oder wie schmal das Band ist.
struct Filt
{
    float s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1, nLP = 1, nBP = 1;
    void reset() { s1 = s2 = 0; }
    // Coefficients have no history: identical inputs need no recalculation.
    float cachedFc = -1, cachedQ = -1, cachedFs = -1;
    void set (float fc, float Q, float fs)
    {
        if (fc == cachedFc && Q == cachedQ && fs == cachedFs) return;
        cachedFc = fc; cachedQ = Q; cachedFs = fs;
        fc = std::clamp (fc, 0.5f, 0.45f * fs);
        Q = std::max (Q, 0.3f);
        const float g = std::tan (kPi * fc / fs);
        k = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        const float half = 0.5f * fs;
        nLP = std::sqrt (half / std::min (half, 1.5707963f * Q * fc));
        nBP = std::sqrt (half / std::min (half, 1.5707963f * fc / Q));
    }
    inline float lp (float x)
    {
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        return v2 * nLP;
    }
    inline float bp (float x)
    {
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        return v1 * k * nBP;
    }
    // Ohne Normierung (Verstaerkung 1 im Durchlassbereich): fuer Signale statt Rauschen, z. B. die Shelter-Wand
    inline float lpRaw (float x)
    {
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        return v2;
    }
    inline void flush() { if (std::fabs (s1) < 1e-20f) s1 = 0; if (std::fabs (s2) < 1e-20f) s2 = 0; }
};

// Einseitiger Tiefpass fuer langsame Zufallskurven (Steuerrate), Ausgang etwa Effektivwert 1
struct SlowNoise
{
    float y = 0, c = 0.01f, norm = 1;
    void setHz (float hz, float tickRate)
    {
        c = 1.0f - std::exp (-2.0f * kPi * hz / tickRate);
        norm = std::sqrt (3.0f * (2.0f - c) / c);
    }
    float tick (Rng& r) { y += c * (r.bi() - y); return y * norm; }
    void reset() { y = 0; }
};

// Tiefes, ungleichmaessiges Druck-Rauschen nach dem Kern von Airwindows "Noise" (Chris Johnson, MIT-Lizenz,
// github.com/airwindows/airwindows, Lizenztext in THIRD_PARTY_NOTICES.md): eine Zufallsbewegung, die in unregelmaessigen Abstaenden (quadratische
// Reste) wieder Richtung Null gezogen wird. Ergibt klumpige, physisch wirkende Druckschuebe statt glattem Rauschen.
struct BrownWalk
{
    double a = 0;
    bool up = true;
    int64_t pos = 0, quad = 0;
    float scale = 1, norm = 1;
    void prepare (float fs) { scale = fs / 44100.0f; norm = 0.01503f; }
    void reset (int64_t start) { a = 0; up = true; pos = start; quad = 0; }
    inline float tick (Rng& r)
    {
        if (--quad < 0)
        {
            ++pos;
            int64_t q = (pos * pos) % 170003; q *= q;
            q %= 17011; q *= q;
            q %= 1151;  q *= q;
            q %= 397;
            quad = (int64_t) ((float) (q + 8) * scale);
            up = a < 0;   // Richtung Mitte
        }
        a += up ? r.uni() : -r.uni();
        return (float) a * norm;
    }
};

//==============================================================================
// Parameter (alle von aussen steuerbar). Die letzten drei sind in Version 1 fest verdrahtet (kein Regler).
struct Params
{
    float gust = 0.3f;        // GUST: laufende Windstaerke 0..1
    float autoGust = 0.0f;    // AUTO GUST: Haeufigkeit zufaelliger Boeen 0..1 (0 = aus)
    bool trigger = false;     // TRIGGER GUST: steigende Flanke loest eine Boee aus
    int env = 0;              // 0 Open, 1 Trees, 2 Rocks, 3 Cracks
    float shelter = 0.0f;     // SHELTER: 0 = draussen, 1 = tief im Gebaeude
    float howl = 0.5f;        // HOWL: Pfeifen und Jaulen
    float baseDb = -32.0f;    // BASE LEVEL: Grundwind in dB (<= -60 = aus)
    float outDb = 0.0f;       // OUTPUT
    float gustShape = 0.5f;   // 0 = schneller Peitschenschlag, 1 = langes Anschwellen (fest 0,5)
    float sweep = 0.6f;       // Staerke der Stereo-Wanderung bei Boeen (fest)
    float rattle = 1.0f;      // Blech-Rappeln im Innenraum (fest)
    bool blow = true;         // BLOW: Wind an/aus (weiches Ein- und Ausblenden)
};

// Boeenform: sanftes Anschwellen (sin^2), dann glattes Abklingen ((1+x)e^-x, ohne Knick am Gipfel)
inline float gustCurve (float t, float A, float D)
{
    if (t <= 0.0f) return 0.0f;
    if (t < A) { const float s = std::sin (0.5f * kPi * t / A); return s * s; }
    const float x = (t - A) / (0.2f * D);
    return (1.0f + x) * std::exp (-x);
}

// Lautstaerkekurve der Boeen-Schicht: Staerke S (0..1) -> dB (Effektivwert, vor OUTPUT)
inline float gustLevelDb (float S)
{
    static const float xs[] = { 0.00f, 0.03f, 0.10f, 0.30f, 0.50f, 0.70f, 1.00f };
    static const float ys[] = { -90.f, -66.f, -54.f, -40.f, -32.f, -26.f, -19.f };
    S = std::clamp (S, 0.0f, 1.0f);
    for (int i = 1; i < 7; ++i)
        if (S <= xs[i]) return lerp (ys[i - 1], ys[i], (S - xs[i - 1]) / (xs[i] - xs[i - 1]));
    return ys[6];
}

// Heulen/Pfeifen je Umgebung: Grundfrequenz, Spanne ueber die Staerke, Exponent, Verhaeltnis 2. Ton, Guete, Gewicht
struct EnvCfg { float fBase, fSpan, pw, ratio, Q, scale; };
inline const EnvCfg& envCfg (int e)
{
    static const EnvCfg cfg[kNumEnvs] = {
        {  300.0f,  560.0f, 1.0f, 1.50f, 18.0f, 1.00f },   // Open: tiefes Stoehnen
        {  560.0f,  900.0f, 1.0f, 1.50f, 22.0f, 0.75f },   // Trees: Singen in den Aesten
        {  420.0f,  900.0f, 1.2f, 1.52f, 26.0f, 1.00f },   // Rocks: Jaulen nach oben, ueber dem hohlen Grundton
        { 1100.0f, 2600.0f, 0.9f, 1.34f, 45.0f, 0.90f } }; // Cracks: schneidendes Pfeifen
    return cfg[std::clamp (e, 0, kNumEnvs - 1)];
}

// Pegelangleichung der Umgebungen (gemessen), damit ein Wechsel nicht lauter oder leiser wird
constexpr float kEnvTrim[kNumEnvs] = { 1.0f, 0.494f, 0.85f, 0.83f };
constexpr float kBedTrim = 0.767f;    // BASE LEVEL in dB entspricht damit etwa der Lautheit (LUFS) des Grundwinds
constexpr float kGustTrim = 0.74f;
constexpr float kRattleGain = 1.6f;

//==============================================================================
struct Ctl   // Steuerwerte, die eine Stimme pro Kanal bekommt
{
    float S = 0, howl = 0, slow = 0, mid = 0, fast = 0;
};

struct Whistle
{
    Filt a, b;
    float amp = 0;
    void set (float f, float Q, float ratio, float fs) { a.set (f, Q, fs); b.set (f * ratio, Q, fs); }
    inline float tick (float w) { return a.bp (w) + 0.55f * b.bp (w); }
};

// Eine Stimme = ein Umgebungs-Charakter in einer Schicht in einem Kanal
struct Voice
{
    Filt f[4], mf[3];
    Whistle wh;
    Rng mr { 1 };
    float g[4] = { 0, 0, 0, 0 };
    float md[3] = { 0, 0, 0 }, mc[3] = { 1, 1, 1 };
    float whAmp = 0, hn = 1, am = 1, nrm = 1;

    void reset (uint32_t seed)
    {
        for (auto& x : f) x.reset();
        for (auto& x : mf) x.reset();
        wh.a.reset(); wh.b.reset();
        mr = Rng (seed);
    }

    void set (int env, const Ctl& c, float fs)
    {
        const float S = c.S;
        const EnvCfg& e = envCfg (env);
        switch (env)
        {
        case 0:   // Open Land: breites tiefes Rauschen, weite Druckwellen
            f[0].set (140.0f + 700.0f * std::pow (S, 0.9f), 0.6f, fs);
            f[1].set (420.0f + 1500.0f * S, 0.8f, fs);
            f[2].set (75.0f, 0.7f, fs);
            f[3].set (3500.0f, 0.6f, fs);
            g[0] = 1.0f; g[1] = 0.55f * (0.4f + 0.6f * S); g[2] = 0.9f * std::pow (S, 1.6f); g[3] = 0.08f * S;
            am = std::max (0.2f, 1.0f + 0.13f * c.slow);
            nrm = 1.0f / std::sqrt (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3]);
            break;
        case 1:   // Trees & Scrub: Laubrauschen. Drei breite Baender (viele Blaetter, kein Einzelknistern), jedes mit
                  // eigener, weicher Schwell-Kurve (verschiedene Aeste); bei Staerke schneller und tiefer bewegt
        {
            f[0].set (1300.0f + 500.0f * S, 0.55f, fs);
            f[1].set (2700.0f + 900.0f * S, 0.65f, fs);
            f[2].set (5200.0f + 800.0f * S, 0.8f, fs);
            f[3].set (220.0f + 260.0f * S, 0.7f, fs);
            g[0] = 1.0f; g[1] = 0.80f; g[2] = 0.30f; g[3] = 0.45f;
            const float rate[3] = { 0.9f + 2.2f * S, 1.4f + 3.2f * S, 2.0f + 4.5f * S };
            const float dep[3]  = { 0.30f + 0.40f * S, 0.30f + 0.38f * S, 0.25f + 0.30f * S };
            for (int k = 0; k < 3; ++k)
            {
                mf[k].set (rate[k], 0.6f, fs);
                md[k] = dep[k];
                mc[k] = std::exp (-dep[k] * dep[k]);   // exp(d*x) mit x ~ N(0,1) hat Effektivwert exp(d^2)
            }
            am = std::max (0.2f, 1.0f + 0.10f * c.slow);
            nrm = 1.0f / std::sqrt (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3]);
            break;
        }
        case 2:   // Rocks & Canyon: hohl-resonant, jault bei Staerke nach oben
        {
            const float f1 = (150.0f + 620.0f * S) * (1.0f + 0.06f * c.mid);
            const float qh = 0.7f + 1.0f * c.howl;   // HOWL macht den hohlen Klang auch resonanter (jaulender)
            f[0].set (f1, 8.0f * qh, fs);
            f[1].set (1.52f * f1, 10.0f * qh, fs);
            f[2].set (2.31f * f1, 13.0f * qh, fs);
            f[3].set (200.0f, 0.7f, fs);
            g[0] = 1.0f; g[1] = 0.75f; g[2] = 0.5f; g[3] = 0.5f;
            am = std::max (0.2f, 1.0f + 0.10f * c.slow);
            nrm = 1.0f / std::sqrt (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3]);
            break;
        }
        default:  // Cracks & Doors: schmalbandige Zugluft
            f[0].set (2500.0f + 3000.0f * S, 0.7f, fs);
            f[1].set (500.0f, 0.7f, fs);
            g[0] = 0.35f * std::sqrt (S); g[1] = 0.25f;
            am = std::max (0.2f, 1.0f + 0.12f * c.slow);
            nrm = 1.0f / std::sqrt (g[0] * g[0] + g[1] * g[1]);
            break;
        }
        // Heulen/Pfeifen (alle Umgebungen, Gewicht je Umgebung)
        // Die Tonhoehe folgt der Staerke und wandert langsam (Stoehnen/Jaulen). HOWL aendert den Charakter, nicht die
        // Lautheit: Rauschen und Ton werden gemeinsam normiert.
        const float fw = (e.fBase + e.fSpan * std::pow (S, e.pw)) * (1.0f + 0.05f * c.mid + 0.015f * c.fast);
        wh.set (fw, e.Q, e.ratio, fs);
        whAmp = c.howl * e.scale * (0.6f + 1.1f * S) * std::clamp (0.6f + 0.4f * c.fast, 0.15f, 1.0f);
        hn = 1.0f / std::sqrt (1.0f + 1.3f * whAmp * whAmp);
    }

    // w: freies weisses Rauschen, wl: teilweise gemeinsames Rauschen fuer die Tiefen (wirkt weit, nicht phasig)
    inline float tick (int env, float w, float wl)
    {
        float y;
        switch (env)
        {
        case 0:  y = (g[0] * f[0].lp (wl) + g[1] * f[1].bp (w) + g[2] * f[2].lp (wl)) * am + g[3] * f[3].bp (w); break;
        case 1:
        {
            const float m0 = std::exp (md[0] * mf[0].lp (mr.white())) * mc[0];
            const float m1 = std::exp (md[1] * mf[1].lp (mr.white())) * mc[1];
            const float m2 = std::exp (md[2] * mf[2].lp (mr.white())) * mc[2];
            y = (g[0] * f[0].bp (w) * m0 + g[1] * f[1].bp (w) * m1 + g[2] * f[2].bp (w) * m2) * am + g[3] * f[3].lp (wl);
            break;
        }
        case 2:  y = (g[0] * f[0].bp (w) + g[1] * f[1].bp (w) + g[2] * f[2].bp (w)) * am + g[3] * f[3].lp (wl); break;
        default: y = g[0] * f[0].bp (w) * am + g[1] * f[1].lp (wl); break;
        }
        return (y * nrm + wh.tick (w) * whAmp) * hn * kEnvTrim[env];
    }
};

// Blech-Rappeln: grosses Wellblech / Blechdach. Viele tiefe, lang klingende Eigenmoden (48 Hz ... 1 kHz), angeregt
// durch Schlaege (Impuls fuer die Tiefe + kurzer Rauschstoss fuer das Scheppern). Eine weiche Saettigung laesst die
// Bleche gegeneinander scheppern.
struct Rattle
{
    static constexpr int N = 10;
    float c1[N] {}, c2[N] {}, gi[N] {}, w[N] {}, y1[N] {}, y2[N] {};
    int cd[kMaxHits] {};
    float amp[kMaxHits] {};
    int count = 0;
    float exc = 0, excDecay = 0.99f;
    Rng ev, nz;

    void prepare (float fs, float detune)
    {
        static const float freq[N] = { 48.0f, 67.0f, 89.0f, 118.0f, 157.0f, 209.0f, 279.0f, 372.0f, 640.0f, 1050.0f };
        static const float t60[N]  = { 1.10f, 0.95f, 0.80f, 0.65f, 0.52f, 0.42f, 0.33f, 0.26f, 0.16f, 0.10f };
        static const float wt[N]   = { 1.00f, 0.95f, 0.90f, 0.80f, 0.70f, 0.60f, 0.50f, 0.40f, 0.22f, 0.12f };
        for (int i = 0; i < N; ++i)
        {
            const float th = 2.0f * kPi * std::min (freq[i] * detune, 0.4f * fs) / fs;
            const float r = std::exp (-6.9078f / (t60[i] * fs));
            c1[i] = 2.0f * r * std::cos (th);
            c2[i] = -r * r;
            gi[i] = std::sin (th);   // Impulsantwort jeder Mode etwa Amplitude 1
            w[i] = wt[i];
        }
        excDecay = std::exp (-1.0f / (0.006f * fs));
    }
    void reset()
    {
        for (int i = 0; i < N; ++i) y1[i] = y2[i] = 0;
        count = 0; exc = 0;
    }
    void push (int delay, float a)
    {
        if (count >= kMaxHits) return;
        cd[count] = std::max (delay, 1); amp[count] = a; ++count;
    }
    inline float tick()
    {
        float imp = 0;
        for (int i = 0; i < count;)
        {
            if (--cd[i] <= 0)
            {
                imp += amp[i];
                exc += 0.6f * std::fabs (amp[i]);
                --count; cd[i] = cd[count]; amp[i] = amp[count];
            }
            else ++i;
        }
        const float in = imp + exc * nz.bi();
        exc *= excDecay;
        float out = 0;
        for (int i = 0; i < N; ++i)
        {
            const float y = c1[i] * y1[i] + c2[i] * y2[i] + in * gi[i];
            y2[i] = y1[i]; y1[i] = y;
            out += y * w[i];
        }
        return std::tanh (0.6f * out);
    }
    void flush()
    {
        for (int i = 0; i < N; ++i) { if (std::fabs (y1[i]) < 1e-20f) y1[i] = 0; if (std::fabs (y2[i]) < 1e-20f) y2[i] = 0; }
        if (exc < 1e-20f) exc = 0;
    }
};

// Lineare Rampe ueber einen Steuerschritt (kein Zippern)
struct Ramp
{
    float v = 0, d = 0;
    void snap (float t) { v = t; d = 0; }
    void to (float t) { d = (t - v) * (1.0f / kCtl); }
    inline float next() { v += d; return v; }
};

struct Gust { int64_t t0 = 0; float A = 1, D = 3, amp = 0, dir = 1; };

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) validRate (sampleRate);
        tickRate = fs / (float) kCtl;
        reset();
    }

    void reset()
    {
        now = 0; phase = 0; primed = false; prevTrigger = false;
        nGusts = 0; act = 0;
        autoLeft = -1; env = 0;
        walkC.prepare (fs); walkC.reset (271); rngWC = Rng (0x27D4EB2Fu);
        onPos = 1.0f;
        rngC = Rng (0x7F4A7C15u); rngCG = Rng (0x5851F42Du); rngEv = Rng (0xC2B2AE35u);
        for (int e = 0; e < kNumEnvs; ++e) { wEnv[e] = e == 0 ? 1.0f : 0.0f; aEnv[e] = wEnv[e]; }
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = chan[c];
            ch.rng = Rng (c == 0 ? 0x1234567u : 0x9E3779B9u);
            ch.rngG = Rng (c == 0 ? 0x2F6B8A1Du : 0x7B3C91E5u);
            for (int e = 0; e < kNumEnvs; ++e)
            {
                ch.bed[e].reset (0x68E31DA4u + 977u * (uint32_t) (e + 4 * c));
                ch.gst[e].reset (0xB5297A4Du + 1009u * (uint32_t) (e + 4 * c));
            }
            ch.walk.prepare (fs); ch.walk.reset (c == 0 ? 11 : 4711);
            ch.rngW = Rng (c == 0 ? 0x1B873593u : 0xCC9E2D51u);
            ch.lp1.reset(); ch.lp2.reset(); ch.sub.reset(); ch.crack.a.reset(); ch.crack.b.reset();
            for (auto& b : ch.boom) b.reset();
            ch.rat.prepare (fs, c == 0 ? 1.0f : 1.011f);
            ch.rat.reset();
            ch.rat.ev = Rng (c == 0 ? 0xA511E9B3u : 0x63D83595u);
            ch.rat.nz = Rng (c == 0 ? 0x85EBCA6Bu : 0xC2B2AE3Du);
            ch.nSlow.reset(); ch.nMid.reset(); ch.nFast.reset();
            ch.nSlow.setHz (0.12f, tickRate); ch.nMid.setHz (0.8f, tickRate); ch.nFast.setHz (5.5f, tickRate);
            ch.nCtl = Rng (c == 0 ? 0x2545F491u : 0x4F6CDD1Du);
            ch.S = ch.Sb = 0;
        }
        cSlow.reset(); cMid.reset(); cFast.reset();
        cSlow.setHz (0.12f, tickRate); cMid.setHz (0.8f, tickRate); cFast.setHz (5.5f, tickRate);
    }

    // Wind wird zum Eingang ADDIERT (der Eingang laeuft unveraendert durch). Zum reinen Erzeugen Nullen uebergeben.
    void process (float* l, float* r, int n, const Params& pIn, unsigned triggerEvents = 0)
    {
        if (n <= 0) return;
        P = sanitize (pIn);
        // Processor events are independent of a held parameter level / chunk boundaries.
        for (unsigned event = 0; event < std::min (triggerEvents, (unsigned) kMaxGusts); ++event)
            spawnGust (0.8f, rngEv.uni() < 0.5f ? -1.0f : 1.0f);
        // Trigger: steigende Flanke
        if (P.trigger && ! prevTrigger) spawnGust (0.8f, rngEv.uni() < 0.5f ? -1.0f : 1.0f);
        prevTrigger = P.trigger;

        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            if (phase == 0) tick();
            const float nC = rngC.white(), nCG = rngCG.white();
            const float pC = walkC.tick (rngWC);
            for (int c = 0; c < 2; ++c)
            {
                auto& ch = chan[c];
                const float w = ch.rng.white();
                const float wl = 0.8f * w + 0.6f * nC;
                const float wg = ch.gustOn ? ch.rngG.white() : 0.0f;
                const float wlg = 0.8f * wg + 0.6f * nCG;
                const float gB = ch.gBed.next(), gG = ch.gGust.next();
                float bed = 0, gst = 0;
                for (int e = 0; e < kNumEnvs; ++e)
                {
                    if (aEnv[e] <= 0.0f) continue;
                    bed += aEnv[e] * ch.bed[e].tick (e, w, wl);
                    if (ch.gustOn) gst += aEnv[e] * ch.gst[e].tick (e, wg, wlg);
                }
                float x = bed * gB + gst * gG;

                // Shelter: Tiefpass, Wand-Wummern, Sub-Grollen
                x = ch.lp2.lpRaw (ch.lp1.lpRaw (x)) * ch.gExt.next();
                float boom = ch.boom[0].bp (wl) + 0.8f * ch.boom[1].bp (wl) + 0.6f * ch.boom[2].bp (wl);
                const float bg = ch.gBoom.next();
                x += boom * bg;
                // Druck auf die Wand: Airwindows-Zufallsbewegung statt glattem Rauschen (eigener + gemeinsamer Anteil)
                const float pw = 0.8f * ch.walk.tick (ch.rngW) + 0.6f * pC;
                x += ch.sub.lpRaw (pw) * ch.gSub.next();
                // Ritzen-Pfeifen (innen)
                x += ch.crack.tick (w) * ch.gCrack.next();
                // Blech-Rappeln
                x += ch.rat.tick() * ch.gRat.next();

                const float y = softClip (x * ch.gOut.next());
                io[c][i] += std::isfinite (y) ? y : 0.0f;
            }
            phase = (phase + 1) & (kCtl - 1);
            ++now;
        }
    }

    float activity() const { return act; }   // 0..1, Boeen-Aktivitaet fuer die LED

private:
    struct Channel
    {
        Rng rng, rngG, nCtl, rngW;
        BrownWalk walk;   // rng: Grundwind, rngG: Boeen-Schicht (unabhaengig, damit sich die Pegel sauber addieren)
        Voice bed[kNumEnvs], gst[kNumEnvs];
        Filt lp1, lp2, sub, boom[3];
        Whistle crack;
        Rattle rat;
        SlowNoise nSlow, nMid, nFast;
        Ramp gBed, gGust, gExt, gBoom, gSub, gCrack, gRat, gOut;
        float S = 0, Sb = 0;
        bool gustOn = false;
    };

    static Params sanitize (Params p)
    {
        p.gust = finiteClamp (p.gust, 0, 1, 0.3f);
        p.autoGust = finiteClamp (p.autoGust, 0, 1, 0);
        p.env = (int) finiteClamp ((float) p.env, 0, kNumEnvs - 1, 0);
        p.shelter = finiteClamp (p.shelter, 0, 1, 0);
        p.howl = finiteClamp (p.howl, 0, 1, 0.5f);
        p.baseDb = finiteClamp (p.baseDb, -80, 6, -32);
        p.outDb = finiteClamp (p.outDb, -60, 24, 0);
        p.gustShape = finiteClamp (p.gustShape, 0, 1, 0.5f);
        p.sweep = finiteClamp (p.sweep, 0, 1, 0.6f);
        p.rattle = finiteClamp (p.rattle, 0, 2, 1);
        p.blow = p.blow;
        return p;
    }

    void spawnGust (float amp, float dir)
    {
        if (nGusts >= kMaxGusts)   // aelteste verdraengen
        {
            for (int i = 1; i < kMaxGusts; ++i) gusts[i - 1] = gusts[i];
            --nGusts;
        }
        Gust& g = gusts[nGusts++];
        g.t0 = now;
        g.A = logLerp (0.2f, 3.0f, P.gustShape);    // 0,5 -> ca. 0,8 s Anstieg
        g.D = logLerp (1.2f, 5.0f, P.gustShape);    // 0,5 -> ca. 2,5 s Abklingen
        g.amp = amp; g.dir = dir;
    }

    static float rateFor (float S)   // Rappel-Ereignisse pro Sekunde
    {
        return S < 0.12f ? 0.0f : 4.5f * std::pow ((S - 0.12f) / 0.88f, 1.3f);
    }

    void tick()
    {
        const float fsT = tickRate;
        const float c = 1.0f - std::exp (-1.0f / (fsT * 0.04f));
        const float cE = 1.0f - std::exp (-1.0f / (fsT * 0.15f));
        if (! primed)
        {
            gustS = P.gust; shelS = P.shelter; howlS = P.howl; baseS = P.baseDb; outS = P.outDb; env = P.env;
            onPos = P.blow ? 1.0f : 0.0f;
            for (int e = 0; e < kNumEnvs; ++e) { wEnv[e] = e == env ? 1.0f : 0.0f; }
        }
        gustS += (P.gust - gustS) * c;
        shelS += (P.shelter - shelS) * c;
        howlS += (P.howl - howlS) * c;
        baseS += (P.baseDb - baseS) * c;
        outS += (P.outDb - outS) * c;
        // BLOW: Wind blendet in ca. 1,5 s ein und klingt in ca. 2,5 s aus (wie ein Wind, der sich legt)
        if (P.blow) onPos = std::min (1.0f, onPos + 1.0f / (1.5f * fsT));
        else        onPos = std::max (0.0f, onPos - 1.0f / (2.5f * fsT));
        const float onGain = onPos * onPos * (3.0f - 2.0f * onPos);
        env = P.env;
        for (int e = 0; e < kNumEnvs; ++e)
        {
            wEnv[e] += ((e == env ? 1.0f : 0.0f) - wEnv[e]) * cE;
            if (wEnv[e] < 1e-3f && e != env) wEnv[e] = 0.0f;
            aEnv[e] = std::sqrt (wEnv[e]);
        }

        // zufaellige Boeen
        if (P.autoGust > 0.0f)
        {
            if (autoLeft < 0)
                autoLeft = (int64_t) ((0.5f + 3.0f * rngEv.uni()) * fs);
            autoLeft -= kCtl;
            if (autoLeft <= 0)
            {
                const float mean = lerp (16.0f, 2.5f, P.autoGust);
                const float u = std::max (rngEv.uni(), 1e-4f);
                const float gap = std::clamp (-mean * std::log (u), 1.0f, 3.0f * mean);
                autoLeft = (int64_t) (gap * fs);
                const float a = 0.30f + 0.55f * rngEv.uni() * (0.4f + 0.6f * P.autoGust);
                spawnGust (a, rngEv.uni() < 0.5f ? -1.0f : 1.0f);
            }
        }
        else autoLeft = -1;

        // Boeen auswerten: pro Kanal zeitversetzte Steuerkurve (links/rechts) -> Wanderung ohne Audio-Verzoegerung
        float prod[2] = { 1.0f, 1.0f };
        float maxEnv = 0;
        for (int i = 0; i < nGusts;)
        {
            const Gust& g = gusts[i];
            const float t = (float) ((double) (now - g.t0) / (double) fs);
            const float off = g.dir * P.sweep * 0.4f;
            if (t - std::fabs (off) > g.A + 1.8f * g.D)
            {
                for (int j = i + 1; j < nGusts; ++j) gusts[j - 1] = gusts[j];
                --nGusts;
                continue;
            }
            const float eL = gustCurve (t + off, g.A, g.D), eR = gustCurve (t - off, g.A, g.D);
            prod[0] *= 1.0f - g.amp * eL;
            prod[1] *= 1.0f - g.amp * eR;
            maxEnv = std::max (maxEnv, g.amp * std::max (eL, eR));
            ++i;
        }
        act = maxEnv;

        // gemeinsame und eigene langsame Schwankungen
        const float cs = cSlow.tick (rngC), cm = cMid.tick (rngC), cf = cFast.tick (rngC);

        const float sh = shelS;
        const float baseLin = baseS <= -59.9f ? 0.0f : dbToLin (baseS) * kBedTrim;
        const bool firstTick = ! primed;

        for (int ci = 0; ci < 2; ++ci)
        {
            auto& ch = chan[ci];
            Ctl ctl;
            ctl.slow = 0.75f * ch.nSlow.tick (ch.nCtl) + 0.66f * cs;
            ctl.mid  = 0.75f * ch.nMid.tick (ch.nCtl) + 0.66f * cm;
            ctl.fast = 0.75f * ch.nFast.tick (ch.nCtl) + 0.66f * cf;
            const float fl = std::clamp (0.6f * ctl.slow + 0.4f * ctl.mid, -2.0f, 2.0f);
            const float g0 = std::clamp (gustS * (1.0f + 0.06f * fl), 0.0f, 1.0f);   // leichte Schwankung der Klangfarbe
            const float S = std::clamp (1.0f - (1.0f - g0) * prod[ci], 0.0f, 1.0f);
            ch.S = S;
            ch.Sb = std::clamp (0.12f + 0.12f * S + 0.03f * fl, 0.0f, 1.0f);
            ch.gustOn = S > 0.003f;
            ctl.howl = howlS;

            // Stimmen aktualisieren
            for (int e = 0; e < kNumEnvs; ++e)
            {
                if (wEnv[e] <= 0.0f) continue;
                Ctl cb = ctl; cb.S = ch.Sb; cb.howl = 0.8f * ctl.howl;   // Grundwind heult mit (etwas weniger)
                ch.bed[e].set (e, cb, fs);
                if (ch.gustOn) { Ctl cg = ctl; cg.S = S; ch.gst[e].set (e, cg, fs); }
            }

            // Pegel
            const float gustLin = ch.gustOn ? dbToLin (gustLevelDb (S) + 1.5f * fl) * kGustTrim : 0.0f;   // organische Pegelschwankung ca. +-1,5 dB

            // Shelter-Kette. Alle Zusatzanteile haengen am aktuellen Windpegel R (Effektivwert), nicht an festen Werten
            const float R = std::sqrt (baseLin * baseLin + gustLin * gustLin);
            float fcLP;
            if (sh < 0.5f) fcLP = logLerp (18000.0f, 2200.0f, sh * 2.0f);
            else           fcLP = logLerp (2200.0f, 350.0f, (sh - 0.5f) * 2.0f);
            ch.lp1.set (fcLP, 0.5412f, fs);
            ch.lp2.set (fcLP, 1.3066f, fs);
            const float extGain = dbToLin (-9.0f * sh);

            const float bell = std::exp (-std::pow ((sh - 0.6f) / 0.28f, 2.0f));
            ch.boom[0].set (62.0f, 4.0f, fs);
            ch.boom[1].set (118.0f, 5.0f, fs);
            ch.boom[2].set (190.0f, 6.0f, fs);
            const float boomGain = R * bell * std::pow (S, 1.2f) * 0.6f * std::clamp (0.7f + 0.3f * ctl.fast, 0.2f, 1.2f);
            ch.sub.set (90.0f, 0.6f, fs);
            const float subGain = R * smoothstep (0.45f, 1.0f, sh) * (0.25f + 0.75f * std::pow (S, 1.1f)) * 0.5f;

            const float fCrack = 1300.0f + 1500.0f * std::pow (S, 0.9f);
            ch.crack.set (fCrack * (1.0f + 0.03f * ctl.mid), 50.0f, 1.34f, fs);
            const float crackGain = R * howlS * std::pow (S, 1.3f) * smoothstep (0.55f, 1.0f, sh) * 0.20f *
                                    std::clamp (0.55f + 0.45f * ctl.fast, 0.1f, 1.0f);

            // Rappeln: nur mit Abstand zum Freien (Shelter), Rate haengt an der Staerke
            const float ratGate = smoothstep (0.10f, 0.55f, sh) * (1.0f - 0.4f * smoothstep (0.75f, 1.0f, sh)) * P.rattle;
            const float rate = rateFor (S) * ratGate;
            if (rate > 0.0f && ch.rat.ev.uni() < rate * (float) kCtl / fs)
            {
                auto& ev = ch.rat.ev;
                const float base = (0.30f + 0.70f * std::pow (ev.uni(), 1.3f)) * (0.40f + 0.60f * S);
                float at = (float) (int) (ev.uni() * kCtl);
                if (ev.uni() < 0.6f)   // Schlackern: eine lose Platte schlaegt mehrmals in Folge an
                {
                    const int nHit = 4 + (int) (ev.uni() * 10.0f * (0.5f + 0.5f * S));
                    const float hz = 9.0f + 14.0f * ev.uni();
                    for (int h = 0; h < nHit; ++h)
                    {
                        const float env = std::sin (kPi * ((float) h + 0.5f) / (float) nHit);
                        ch.rat.push ((int) at, base * (0.35f + 0.65f * env) * (ev.bi() < 0 ? -1.0f : 1.0f));
                        at += fs / hz * (0.75f + 0.5f * ev.uni());
                    }
                }
                else   // schwerer Schlag mit Nachfedern
                {
                    const float sg = ev.bi() < 0 ? -1.0f : 1.0f;
                    ch.rat.push ((int) at, 1.4f * base * sg);
                    at += (0.07f + 0.05f * ev.uni()) * fs;
                    ch.rat.push ((int) at, -0.5f * base * sg);
                    at += (0.05f + 0.04f * ev.uni()) * fs;
                    ch.rat.push ((int) at, 0.25f * base * sg);
                }
            }
            const float ratGain = R * ratGate * kRattleGain;
            ch.rat.flush();
            ch.lp1.flush(); ch.lp2.flush(); ch.sub.flush();
            for (auto& b : ch.boom) b.flush();

            const float outG = dbToLin (outS) * onGain;
            if (firstTick)
            {
                ch.gBed.snap (baseLin); ch.gGust.snap (gustLin); ch.gExt.snap (extGain); ch.gBoom.snap (boomGain);
                ch.gSub.snap (subGain); ch.gCrack.snap (crackGain); ch.gRat.snap (ratGain); ch.gOut.snap (outG);
            }
            else
            {
                ch.gBed.to (baseLin); ch.gGust.to (gustLin); ch.gExt.to (extGain); ch.gBoom.to (boomGain);
                ch.gSub.to (subGain); ch.gCrack.to (crackGain); ch.gRat.to (ratGain); ch.gOut.to (outG);
            }
        }
        primed = true;
    }

    float fs = 48000.0f, tickRate = 3000.0f;
    Params P;
    Channel chan[2];
    Rng rngC { 1 }, rngCG { 1 }, rngEv { 1 };
    SlowNoise cSlow, cMid, cFast;
    Gust gusts[kMaxGusts];
    int nGusts = 0;
    int64_t now = 0, autoLeft = -1;
    int phase = 0, env = 0;
    bool primed = false, prevTrigger = false;
    float gustS = 0.3f, shelS = 0, howlS = 0.5f, baseS = -32, outS = 0;
    float wEnv[kNumEnvs] {}, aEnv[kNumEnvs] {};
    float act = 0, onPos = 1.0f;
    BrownWalk walkC;
    Rng rngWC { 1 };
};

}   // namespace gale
