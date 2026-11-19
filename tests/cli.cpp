// Консольная проверка ядра: kbd_cli file.wav [--nohpss] [--harm N] [--progress]
#include "../Source/core/KeyBpmCore.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static bool readWav (const char* path, std::vector<float>& mono, double& sr)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4)) return false;

    size_t p = 12;
    int fmt = 0, ch = 0, bits = 0;
    while (p + 8 <= d.size())
    {
        uint32_t sz;
        std::memcpy (&sz, d.data() + p + 4, 4);
        if (! std::memcmp (d.data() + p, "fmt ", 4))
        {
            uint16_t f16, c16, b16;
            uint32_t r32;
            std::memcpy (&f16, d.data() + p + 8, 2);
            std::memcpy (&c16, d.data() + p + 10, 2);
            std::memcpy (&r32, d.data() + p + 12, 4);
            std::memcpy (&b16, d.data() + p + 22, 2);
            fmt = f16; ch = c16; sr = r32; bits = b16;
            if (fmt == 0xFFFE) { uint16_t sub; std::memcpy (&sub, d.data() + p + 32, 2); fmt = sub; }
        }
        else if (! std::memcmp (d.data() + p, "data", 4))
        {
            const size_t n = std::min<size_t> (sz, d.size() - p - 8) / (size_t) (bits / 8) / (size_t) ch;
            mono.resize (n);
            const char* s = d.data() + p + 8;
            for (size_t i = 0; i < n; ++i)
            {
                float acc = 0;
                for (int c = 0; c < ch; ++c)
                {
                    const char* q = s + (i * (size_t) ch + (size_t) c) * (size_t) (bits / 8);
                    float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy (&v, q, 4);
                    else if (bits == 16) { int16_t x; std::memcpy (&x, q, 2); v = x / 32768.0f; }
                    else if (bits == 24) { int32_t x = (uint8_t) q[0] | ((uint8_t) q[1] << 8) | ((int8_t) q[2] << 16); v = x / 8388608.0f; }
                    else if (bits == 32) { int32_t x; std::memcpy (&x, q, 4); v = x / 2147483648.0f; }
                    acc += v;
                }
                mono[i] = acc / ch;
            }
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: kbd_cli file.wav [--nohpss] [--harm N] [--progress]\n"); return 1; }

    std::vector<float> x;
    double sr = 44100;
    if (! readWav (argv[1], x, sr)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }

    kbd::Analyzer a;
    bool progress = false;
    for (int i = 2; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--nohpss")) a.options.hpss = false;
        else if (! std::strcmp (argv[i], "--harm") && i + 1 < argc) a.options.harmonics = std::atoi (argv[++i]);
        else if (! std::strcmp (argv[i], "--progress")) progress = true;
        else if (! std::strcmp (argv[i], "--prof") && i + 1 < argc) a.options.profileMask = std::atoi (argv[++i]);
    }
    a.prepare (sr);

    const int block = 512;
    double nextReport = 5.0;
    for (size_t i = 0; i < x.size(); i += block)
    {
        a.process (x.data() + i, (int) std::min<size_t> (block, x.size() - i));
        if (progress && (double) (i + block) / sr >= nextReport)
        {
            auto k = a.computeKey();
            auto t = a.computeTempo();
            std::printf ("  t=%5.1fs key=%s (%.2f) bpm=%.2f (%.2f)\n", nextReport,
                         k.valid ? kbd::keyShort (k.tonic, k.minor).c_str() : "-", k.confidence,
                         t.bpm, t.confidence);
            nextReport += 5.0;
        }
    }

    auto k = a.computeKey();
    auto t = a.computeTempo();
    std::printf ("KEY %s REL %s ALT %s CONF %.2f BAL %.2f TUNE %+.0f BPM %.3f TCONF %.2f\n",
                 kbd::keyShort (k.tonic, k.minor).c_str(),
                 kbd::keyShort (kbd::relativeTonic (k.tonic, k.minor), ! k.minor).c_str(),
                 kbd::keyShort (k.altTonic, k.altMinor).c_str(),
                 k.confidence, k.pairBalance, k.tuningCents, t.bpm, t.confidence);
    {
        double m = 0, v = 0;
        for (float c : k.chroma) m += c;
        m /= 12;
        for (float c : k.chroma) v += (c - m) * (c - m);
        const int rt = kbd::relativeTonic (k.tonic, k.minor);
        const float sMin = k.minor ? k.scores[(size_t) (12 + k.tonic)] : k.scores[(size_t) (12 + rt)];
        const float sMaj = k.minor ? k.scores[(size_t) rt] : k.scores[(size_t) k.tonic];
        std::printf ("PAIRDIFF %.4f\n", sMin - sMaj);
        std::printf ("CV %.3f BEST %.3f\n", std::sqrt (v / 12) / m, k.scores[(size_t) (k.minor ? 12 + k.tonic : k.tonic)]);
    }
    return 0;
}
