#include HEADER
#include <fstream>
#include <xmmintrin.h>
int main (int argc, char** argv)
{
    if (argc != 2) return 1;
    _mm_setcsr (_mm_getcsr() | 0x8040);
    std::ofstream file (argv[1], std::ios::binary);
    for (double fs : { 22050., 44100., 48000., 96000., 192000. })
    for (int env = 0; env < 4; ++env)
    {
        gale::Engine engine; engine.prepare (fs); gale::Params p;
        p.env = env; p.shelter = 0.6f; p.autoGust = 0.7f;
        float l[127] {}, r[127] {};
        for (int pos = 0; pos < (int) fs * 6;)
        {
            const int n = std::min (127, (int) fs * 6 - pos);
            p.blow = pos < (int) fs || pos >= (int) fs * 4;
            p.trigger = pos == 0;
            p.gust = 0.2f + 0.3f * std::sin (pos / (float) fs) * std::sin (pos / (float) fs);
            std::fill_n (l, n, 0); std::fill_n (r, n, 0);
            engine.process (l, r, n, p);
            file.write (reinterpret_cast<const char*> (l), n * sizeof (float));
            file.write (reinterpret_cast<const char*> (r), n * sizeof (float));
            pos += n;
        }
    }
    return file ? 0 : 1;
}
