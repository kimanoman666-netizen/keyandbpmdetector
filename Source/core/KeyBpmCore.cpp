#include "KeyBpmCore.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kbd
{

static constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// FFT
FFT::FFT (int size) : n (size), buf ((size_t) size), tw ((size_t) size / 2), rev ((size_t) size)
{
    int bits = 0;
    while ((1 << bits) < n) ++bits;

    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev[(size_t) i] = r;
    }

    for (int i = 0; i < n / 2; ++i)
        tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
}

void FFT::magnitude (const float* in, float* mag)
{
    for (int i = 0; i < n; ++i)
        buf[(size_t) rev[(size_t) i]] = { in[i], 0.0f };

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                auto u = buf[(size_t) (i + j)];
                auto v = buf[(size_t) (i + j + half)] * tw[(size_t) (j * step)];
                buf[(size_t) (i + j)] = u + v;
                buf[(size_t) (i + j + half)] = u - v;
            }
        }
    }

    for (int k = 0; k <= n / 2; ++k)
        mag[k] = std::abs (buf[(size_t) k]);
}

//==============================================================================
// Decimator
void Decimator::prepare (double inputRate)
{
    D = std::max (1, (int) std::lround (inputRate / 11025.0));
    outRate = inputRate / D;

    if (D > 1)
    {
        L = 24 * D + 1;
        taps.assign ((size_t) L, 0.0f);
        const double fc = 0.45 / D; // частота среза относительно входной fs
        const int M = (L - 1) / 2;
        double sum = 0;
        for (int i = 0; i < L; ++i)
        {
            const double x = i - M;
            const double sinc = x == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (L - 1)) + 0.08 * std::cos (4.0 * kPi * i / (L - 1));
            taps[(size_t) i] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (auto& t : taps) t = (float) (t / sum);
    }
    else
    {
        L = 1;
        taps.assign (1, 1.0f);
    }
    reset();
}

void Decimator::reset()
{
    hist.assign ((size_t) (2 * L), 0.0f);
    pos = 0;
    phase = 0;
}

int Decimator::process (const float* in, int n, float* out)
{
    if (D == 1)
    {
        std::copy (in, in + n, out);
        return n;
    }

    int produced = 0;
    for (int i = 0; i < n; ++i)
    {
        hist[(size_t) pos] = hist[(size_t) (pos + L)] = in[i];
        pos = (pos + 1) % L;

        if (++phase == D)
        {
            phase = 0;
            const float* h = hist.data() + pos;
            float y = 0;
            for (int j = 0; j < L; ++j)
                y += taps[(size_t) j] * h[j];
            out[produced++] = y;
        }
    }
    return produced;
}

//==============================================================================
// Названия
static const char* kNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* kNotesRu[12] = { "до", "до-диез", "ре", "ре-диез", "ми", "фа", "фа-диез", "соль", "соль-диез", "ля", "ля-диез", "си" };

std::string noteName (int pc) { return kNotes[((pc % 12) + 12) % 12]; }

std::string keyName (int tonic, bool minor) { return noteName (tonic) + (minor ? " minor" : " major"); }

std::string keyShort (int tonic, bool minor) { return noteName (tonic) + (minor ? "m" : ""); }

std::string keyNameRu (int tonic, bool minor)
{
    return std::string (kNotesRu[((tonic % 12) + 12) % 12]) + (minor ? " минор" : " мажор");
}

int relativeTonic (int tonic, bool minor) { return minor ? (tonic + 3) % 12 : (tonic + 9) % 12; }

std::string camelot (int tonic, bool minor)
{
    // мажор: C=8B, G=9B ... ; минор: Am=8A, Em=9A ...
    const int majorTonic = minor ? relativeTonic (tonic, true) : tonic;
    const int num = ((majorTonic * 7) % 12 + 7) % 12 + 1; // C -> 8
    return std::to_string (num) + (minor ? "A" : "B");
}

std::vector<int> scaleNotes (int tonic, bool minor)
{
    static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
    std::vector<int> r;
    for (int i = 0; i < 7; ++i)
        r.push_back ((tonic + (minor ? mnr[i] : maj[i])) % 12);
    return r;
}

//==============================================================================
// Профили тональностей (от тоники)
namespace
{
struct Profile { double major[12]; double minor[12]; };

const Profile kProfiles[] = {
    // Krumhansl–Kessler
    { { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
      { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } },
    // Temperley (Kostka–Payne)
    { { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 },
      { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 } },
    // Sha'ath (KeyFinder) — лучше для современной музыки
    { { 6.6, 2.0, 3.5, 2.3, 4.6, 4.0, 2.5, 5.2, 2.4, 3.7, 2.3, 3.4 },
      { 6.5, 2.7, 3.5, 5.4, 2.6, 3.5, 2.5, 5.2, 4.0, 2.7, 4.3, 3.2 } },
};

double pearson (const double* a, const double* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i)
    {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
}

float rms (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return (float) std::sqrt (s / std::max (1, n));
}

// Линейная интерполяция по массиву
inline float sampleAt (const std::vector<float>& e, double t)
{
    if (t < 0) return 0;
    const auto i = (size_t) t;
    if (i + 1 >= e.size()) return 0;
    const float f = (float) (t - (double) i);
    return e[i] + f * (e[i + 1] - e[i]);
}
} // namespace

//==============================================================================
Analyzer::Analyzer() { prepare (44100.0); }

void Analyzer::prepare (double sampleRate)
{
    dec.prepare (sampleRate);
    fs = dec.outputRate();

    kN = fs < 14000 ? 8192 : 16384;
    kHop = kN / 8;
    kFft = std::make_unique<FFT> (kN);
    kWin.resize ((size_t) kN);
    for (int i = 0; i < kN; ++i) // Blackman–Harris
    {
        const double x = 2.0 * kPi * i / (kN - 1);
        kWin[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
    }
    kMag.resize ((size_t) kN / 2 + 1);

    oN = fs < 14000 ? 512 : 1024;
    oHop = fs < 14000 ? 64 : 128;
    frameRate = fs / oHop;
    oFft = std::make_unique<FFT> (oN);
    oWin.resize ((size_t) oN);
    for (int i = 0; i < oN; ++i)
        oWin[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (oN - 1)));
    oMag.resize ((size_t) oN / 2 + 1);
    oPrevLog.resize ((size_t) oN / 2 + 1);

    reset();
}

void Analyzer::reset()
{
    dec.reset();
    kBuf.clear();
    kHist.assign (hpssK, std::vector<float> ((size_t) kN / 2 + 1, 0.0f));
    kHistSilent.assign (hpssK, 1);
    kHistCount = kHistPos = 0;
    pcHist.fill (0.0);
    keyFrames = 0;

    oBuf.clear();
    oHavePrev = false;
    segments.clear();
    current.clear();
    silentRun = 0;
    activeFrames = 0;
}

double Analyzer::secondsAnalyzed() const { return activeFrames / frameRate; }

void Analyzer::process (const float* mono, int n)
{
    const int chunk = 4096;
    if ((int) scratch.size() < chunk) scratch.resize (chunk);

    while (n > 0)
    {
        const int m = std::min (n, chunk);
        const int produced = dec.process (mono, m, scratch.data());
        processInternal (scratch.data(), produced);
        mono += m;
        n -= m;
    }
}

void Analyzer::processInternal (const float* x, int n)
{
    if (n <= 0) return;
    kBuf.insert (kBuf.end(), x, x + n);
    oBuf.insert (oBuf.end(), x, x + n);

    while ((int) kBuf.size() >= kN)
    {
        keyFrame();
        kBuf.erase (kBuf.begin(), kBuf.begin() + kHop);
    }
    while ((int) oBuf.size() >= oN)
    {
        onsetFrame();
        oBuf.erase (oBuf.begin(), oBuf.begin() + oHop);
    }
}

//==============================================================================
// Тональность: спектр -> (HPSS) -> пики -> гистограмма классов высоты 10 центов
void Analyzer::keyFrame()
{
    const bool silent = rms (kBuf.data(), kN) < 0.001f;

    std::vector<float> frame ((size_t) kN);
    for (int i = 0; i < kN; ++i) frame[(size_t) i] = kBuf[(size_t) i] * kWin[(size_t) i];
    kFft->magnitude (frame.data(), kMag.data());

    const int bins = kN / 2 + 1;
    const int kMin = std::max (2, (int) std::floor (options.minFreq * kN / fs));
    const int kMax = std::min (bins - 2, (int) std::ceil (options.maxFreq * kN / fs));

    std::vector<float> spec;

    if (options.hpss)
    {
        kHist[(size_t) kHistPos] = kMag;
        kHistSilent[(size_t) kHistPos] = silent ? 1 : 0;
        kHistPos = (kHistPos + 1) % hpssK;
        ++kHistCount;
        if (kHistCount < hpssK) return;

        const int c = (kHistPos + hpssK / 2) % hpssK; // центральный кадр
        if (kHistSilent[(size_t) c]) return;

        const auto& S = kHist[(size_t) c];
        spec.assign ((size_t) bins, 0.0f);
        const int fHalf = 8;
        float tmp[hpssK];
        std::vector<float> ftmp (2 * fHalf + 1);

        for (int k = kMin - 1; k <= kMax + 1; ++k)
        {
            for (int t = 0; t < hpssK; ++t) tmp[t] = kHist[(size_t) t][(size_t) k];
            std::nth_element (tmp, tmp + hpssK / 2, tmp + hpssK);
            const float H = tmp[hpssK / 2];

            int cnt = 0;
            for (int j = k - fHalf; j <= k + fHalf; ++j)
                if (j >= 0 && j < bins) ftmp[(size_t) cnt++] = S[(size_t) j];
            std::nth_element (ftmp.begin(), ftmp.begin() + cnt / 2, ftmp.begin() + cnt);
            const float P = ftmp[(size_t) cnt / 2];

            const float h2 = H * H, p2 = P * P;
            spec[(size_t) k] = S[(size_t) k] * (h2 / (h2 + p2 + 1e-12f));
        }
    }
    else
    {
        if (silent) return;
        spec = kMag;
    }

    // Пики
    float maxMag = 0;
    for (int k = kMin; k <= kMax; ++k) maxMag = std::max (maxMag, spec[(size_t) k]);
    if (maxMag <= 0) return;
    const float thr = maxMag * 0.001f; // -60 дБ

    struct Peak { float f, a; };
    std::vector<Peak> peaks;
    for (int k = kMin; k <= kMax; ++k)
    {
        const float b = spec[(size_t) k];
        if (b > thr && b > spec[(size_t) k - 1] && b >= spec[(size_t) k + 1])
        {
            const float a = std::log (spec[(size_t) k - 1] + 1e-12f);
            const float bl = std::log (b + 1e-12f);
            const float c = std::log (spec[(size_t) k + 1] + 1e-12f);
            const float den = a - 2 * bl + c;
            const float d = den != 0 ? 0.5f * (a - c) / den : 0.0f;
            const float amp = std::exp (bl - 0.25f * (a - c) * d);
            peaks.push_back ({ (float) ((k + d) * fs / kN), amp });
        }
    }
    if (peaks.empty()) return;
    if (peaks.size() > 80)
    {
        std::nth_element (peaks.begin(), peaks.begin() + 80, peaks.end(),
                          [] (const Peak& x, const Peak& y) { return x.a > y.a; });
        peaks.resize (80);
    }

    std::array<double, 120> local {};
    for (const auto& p : peaks)
    {
        for (int h = 1; h <= std::max (1, options.harmonics); ++h)
        {
            const double f0 = p.f / h;
            if (f0 < 25.0) break;
            const double w = p.a * std::pow (0.6, h - 1);
            const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
            double pc = std::fmod (midi, 12.0);
            if (pc < 0) pc += 12.0;
            const double pos = pc * 10.0;
            const int i0 = (int) std::floor (pos);
            const double fr = pos - i0;
            local[(size_t) (i0 % 120)] += w * (1.0 - fr);
            local[(size_t) ((i0 + 1) % 120)] += w * fr;
        }
    }

    const double mx = *std::max_element (local.begin(), local.end());
    if (mx <= 0) return;
    for (int i = 0; i < 120; ++i) pcHist[(size_t) i] += local[(size_t) i] / mx;
    ++keyFrames;
}

KeyResult Analyzer::computeKey() const
{
    KeyResult r;
    if (keyFrames < 4) return r;

    // Подстройка строя: среднее положение пиков внутри полутона
    double sx = 0, sy = 0;
    for (int b = 0; b < 120; ++b)
    {
        const double ang = 2.0 * kPi * (b % 10) / 10.0;
        sx += pcHist[(size_t) b] * std::cos (ang);
        sy += pcHist[(size_t) b] * std::sin (ang);
    }
    const double offset = (sx == 0 && sy == 0) ? 0.0 : std::atan2 (sy, sx) / (2.0 * kPi); // полутоны, -0.5..0.5
    r.tuningCents = (float) (offset * 100.0);

    // 12 классов с окном cos^2 шириной 4/3 полутона
    double chroma[12] = {};
    const double l = 4.0 / 3.0;
    for (int b = 0; b < 120; ++b)
    {
        const double s = b / 10.0;
        for (int k = 0; k < 12; ++k)
        {
            double d = s - (k + offset);
            d = std::fmod (d + 18.0, 12.0) - 6.0;
            if (std::abs (d) < l / 2)
            {
                const double w = std::cos (kPi * d / l);
                chroma[k] += pcHist[(size_t) b] * w * w;
            }
        }
    }
    const double cmax = *std::max_element (chroma, chroma + 12);
    for (int k = 0; k < 12; ++k) r.chroma[(size_t) k] = (float) (cmax > 0 ? chroma[k] / cmax : 0);

    // Корреляция с профилями
    const int nProfiles = (int) (sizeof (kProfiles) / sizeof (kProfiles[0]));
    for (int key = 0; key < 24; ++key)
    {
        const bool minor = key >= 12;
        const int t = key % 12;
        double s = 0;
        int used = 0;
        for (int p = 0; p < nProfiles; ++p)
        {
            if (! (options.profileMask & (1 << p))) continue;
            ++used;
            double prof[12];
            for (int i = 0; i < 12; ++i)
                prof[(i + t) % 12] = minor ? kProfiles[p].minor[i] : kProfiles[p].major[i];
            s += pearson (chroma, prof, 12);
        }
        r.scores[(size_t) key] = (float) (s / std::max (1, used));
    }

    const int best = (int) (std::max_element (r.scores.begin(), r.scores.end()) - r.scores.begin());
    r.minor = best >= 12;
    r.tonic = best % 12;
    const int rel = r.minor ? relativeTonic (r.tonic, true) : 12 + relativeTonic (r.tonic, false);

    int alt = -1;
    for (int k = 0; k < 24; ++k)
        if (k != best && k != rel && (alt < 0 || r.scores[(size_t) k] > r.scores[(size_t) alt]))
            alt = k;
    r.altMinor = alt >= 12;
    r.altTonic = alt % 12;

    const float sb = r.scores[(size_t) best], sr = r.scores[(size_t) rel], sa = r.scores[(size_t) alt];
    // Уверенность: отрыв лучшей пары от следующего варианта
    double mean = 0, var = 0;
    for (int k = 0; k < 12; ++k) mean += chroma[k];
    mean /= 12;
    for (int k = 0; k < 12; ++k) var += (chroma[k] - mean) * (chroma[k] - mean);
    const double cv = mean > 0 ? std::sqrt (var / 12) / mean : 0.0;     // «тональность» сигнала (у шума ~0.15)
    const double seconds = (double) keyFrames * kHop / fs;
    r.confidence = std::clamp ((sb - sa) / 0.12f, 0.0f, 1.0f)
                 * std::clamp ((sb - 0.5f) / 0.3f, 0.0f, 1.0f)
                 * (float) std::clamp ((cv - 0.15) / 0.15, 0.0, 1.0)
                 * (float) std::clamp (seconds / 12.0, 0.15, 1.0);
    r.pairBalance = 1.0f / (1.0f + std::exp (-(sb - sr) * 40.0f));
    r.valid = true;
    return r;
}

//==============================================================================
// Темп: огибающая атак (spectral flux)
void Analyzer::onsetFrame()
{
    const float level = rms (oBuf.data() + (oN - oHop), oHop);
    const bool silent = level < 0.0018f; // ~ -55 дБ

    if (silent)
    {
        ++silentRun;
        if (silentRun > (int) (frameRate * 0.5))
        {
            closeSegment();
            oHavePrev = false;
            return;
        }
        if (! current.empty())
        {
            current.push_back (0.0f);
        }
        return;
    }
    silentRun = 0;

    std::vector<float> frame ((size_t) oN);
    for (int i = 0; i < oN; ++i) frame[(size_t) i] = oBuf[(size_t) i] * oWin[(size_t) i];
    oFft->magnitude (frame.data(), oMag.data());

    const int bins = oN / 2 + 1;
    const float norm = 2.0f / (oN * 0.5f);
    float flux = 0;
    for (int k = 1; k < bins; ++k)
    {
        const float lg = std::log1p (100.0f * oMag[(size_t) k] * norm);
        if (oHavePrev) flux += std::max (0.0f, lg - oPrevLog[(size_t) k]);
        oPrevLog[(size_t) k] = lg;
    }
    oHavePrev = true;

    current.push_back (flux);
    ++activeFrames;

    const size_t cap = (size_t) (frameRate * 300.0);
    if (current.size() > cap)
        current.erase (current.begin(), current.begin() + (long) (frameRate * 30.0));
}

void Analyzer::closeSegment()
{
    if (current.size() > (size_t) (frameRate * 1.0))
        segments.push_back (current);
    current.clear();

    // ограничим общий объём
    size_t total = 0;
    for (auto& s : segments) total += s.size();
    while (segments.size() > 1 && total > (size_t) (frameRate * 300.0))
    {
        total -= segments.front().size();
        segments.erase (segments.begin());
    }
}

TempoResult Analyzer::computeTempo() const
{
    TempoResult r;

    // Берём последние ~120 с звука
    std::vector<const std::vector<float>*> src;
    if (current.size() > (size_t) (frameRate * 1.0)) src.push_back (&current);
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) src.push_back (&*it);

    std::vector<std::vector<float>> segs;
    size_t total = 0;
    const size_t limit = (size_t) (frameRate * 120.0);
    for (auto* s : src)
    {
        if (total >= limit) break;
        const size_t take = std::min (s->size(), limit - total);
        segs.emplace_back (s->end() - (long) take, s->end());
        total += take;
    }
    if (total < (size_t) (frameRate * 2.5)) return r;

    // Предобработка: вычитаем скользящее среднее, однополупериодное выпрямление, сглаживание
    const int W = (int) (frameRate * 0.25);
    for (auto& e : segs)
    {
        const int n = (int) e.size();
        std::vector<double> pre ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + e[(size_t) i];
        std::vector<float> o ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const int a = std::max (0, i - W), b = std::min (n, i + W + 1);
            const float m = (float) ((pre[(size_t) b] - pre[(size_t) a]) / (b - a));
            o[(size_t) i] = std::max (0.0f, e[(size_t) i] - m);
        }
        // гаусс σ ≈ 1 кадр
        std::vector<float> sm ((size_t) n);
        const float g[5] = { 0.054f, 0.242f, 0.399f, 0.242f, 0.054f };
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            for (int j = -2; j <= 2; ++j)
            {
                const int k = std::clamp (i + j, 0, n - 1);
                s += g[j + 2] * o[(size_t) k];
            }
            sm[(size_t) i] = s;
        }
        // нормировка
        double mean = 0;
        for (float v : sm) mean += v;
        mean /= std::max (1, n);
        if (mean > 0)
            for (auto& v : sm) v = (float) (v / mean);
        e.swap (sm);
    }

    // --- Грубая оценка: автокорреляция + гребёнка ---
    const double minBpm = 60.0, maxBpm = 200.0;
    const int maxLag = (int) std::ceil (4.0 * frameRate * 60.0 / minBpm) + 4;
    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    for (auto& e : segs)
    {
        const int n = (int) e.size();
        double mean = 0;
        for (float v : e) mean += v;
        mean /= n;
        for (int lag = 0; lag <= maxLag && lag < n; ++lag)
        {
            double s = 0;
            const float* a = e.data();
            const float* b = e.data() + lag;
            const int m = n - lag;
            for (int i = 0; i < m; ++i) s += (a[i] - mean) * (b[i] - mean);
            acf[(size_t) lag] += s / n;
        }
    }
    if (acf[0] <= 0) return r;
    for (auto& v : acf) v /= acf[0];

    auto acfAt = [&] (double lag) {
        if (lag < 0 || lag >= maxLag) return 0.0;
        const int i = (int) lag;
        const double f = lag - i;
        return acf[(size_t) i] * (1 - f) + acf[(size_t) i + 1] * f;
    };

    // Гребёнка по кратным лагам. Октавные ошибки (x2, ÷2) здесь не важны —
    // результат потом приводится к диапазону [rangeLo, 2*rangeLo).
    double bestT = 120, bestS = -1e9;
    for (double T = minBpm; T < maxBpm; T += 0.25)
    {
        const double lag = frameRate * 60.0 / T;
        double comb = 0;
        for (int k = 1; k <= 4; ++k)
            comb += acfAt (k * lag);
        if (comb > bestS) { bestS = comb; bestT = T; }
    }
    while (bestT < rangeLo) bestT *= 2;
    while (bestT >= 2 * rangeLo) bestT /= 2;

    // --- Точное уточнение: подгонка сетки долей по фазе ---
    auto gridScore = [&] (double bpm, double* clarity) {
        const double P = frameRate * 60.0 / bpm;
        double acc = 0, accMean = 0;
        for (auto& e : segs)
        {
            const int n = (int) e.size();
            const int beats = (int) ((n - 2) / P) - 1;
            if (beats < 2) continue;
            double best = 0, sum = 0;
            int cnt = 0;
            for (double ph = 0; ph < P; ph += 0.5)
            {
                double s = 0;
                for (int b = 0; b < beats; ++b)
                {
                    const double t = ph + b * P;
                    s += sampleAt (e, t) + 0.5 * sampleAt (e, t + 0.5 * P);
                }
                s /= beats;
                best = std::max (best, s);
                sum += s;
                ++cnt;
            }
            const double w = (double) n;
            acc += best * w;
            accMean += (sum / std::max (1, cnt)) * w;
        }
        if (clarity) *clarity = acc > 0 ? (acc - accMean) / acc : 0.0;
        return acc;
    };

    auto search = [&] (double lo, double hi, double step) {
        double bT = lo, bS = -1;
        std::vector<double> ts, ss;
        for (double T = lo; T <= hi + 1e-9; T += step)
        {
            const double s = gridScore (T, nullptr);
            ts.push_back (T);
            ss.push_back (s);
            if (s > bS) { bS = s; bT = T; }
        }
        // параболическая интерполяция
        const auto it = std::find (ts.begin(), ts.end(), bT);
        const size_t i = (size_t) (it - ts.begin());
        if (i > 0 && i + 1 < ss.size())
        {
            const double a = ss[i - 1], b = ss[i], c = ss[i + 1];
            const double den = a - 2 * b + c;
            if (den < 0) bT += 0.5 * (a - c) / den * step;
        }
        return bT;
    };

    double T = search (bestT * 0.97, bestT * 1.03, 0.05);
    T = search (T - 0.08, T + 0.08, 0.005);

    double clarity = 0;
    gridScore (T, &clarity);

    // Насколько выражен пик автокорреляции
    const double lag0 = frameRate * 60.0 / T;
    const double peak = std::max ({ acfAt (lag0), acfAt (2 * lag0), acfAt (0.5 * lag0) });

    r.bpm = T;
    r.confidence = (float) (std::clamp (0.5 * std::clamp ((clarity - 0.2) / 0.5, 0.0, 1.0)
                                        + 0.5 * std::clamp (peak / 0.3, 0.0, 1.0), 0.0, 1.0)
                            * std::clamp ((double) total / frameRate / 10.0, 0.2, 1.0));
    r.valid = true;
    return r;
}

} // namespace kbd
