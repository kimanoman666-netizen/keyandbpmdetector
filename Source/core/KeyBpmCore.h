// KeyBpmCore — анализ тональности и темпа без зависимостей от JUCE.
// Используется и в плагине, и в консольном тесте (tests/cli.cpp).
#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kbd
{

//==============================================================================
// Простое FFT (radix-2) для вещественного сигнала -> модуль спектра.
class FFT
{
public:
    explicit FFT (int size);
    int size() const { return n; }
    // in: n отсчётов, mag: n/2 + 1 значений
    void magnitude (const float* in, float* mag);

private:
    int n;
    std::vector<std::complex<float>> buf, tw;
    std::vector<int> rev;
};

//==============================================================================
// Понижение частоты дискретизации до ~11–12 кГц (ФНЧ + прореживание).
class Decimator
{
public:
    void prepare (double inputRate);
    double outputRate() const { return outRate; }
    int factor() const { return D; }
    void reset();
    // Возвращает количество выходных отсчётов, записанных в out.
    int process (const float* in, int n, float* out);

private:
    int D = 1;
    double outRate = 44100.0;
    std::vector<float> taps, hist;
    int L = 0, pos = 0, phase = 0;
};

//==============================================================================
struct KeyResult
{
    bool valid = false;
    int tonic = 0;          // 0 = C, 1 = C#, ... 11 = B
    bool minor = true;
    int altTonic = 0;       // второй по вероятности вариант (не параллельная)
    bool altMinor = true;
    float confidence = 0;   // 0..1 — уверенность в паре «тональность / параллельная»
    float pairBalance = 0.5f; // 0..1 — насколько основная вероятнее параллельной
    float tuningCents = 0;  // отклонение строя от A=440, центы
    std::array<float, 12> chroma {};
    std::array<float, 24> scores {}; // 0..11 мажор, 12..23 минор
};

struct TempoResult
{
    bool valid = false;
    double bpm = 0;
    float confidence = 0;   // 0..1
};

// Вспомогательные функции для отображения
std::string noteName (int pc);                        // "C#"
std::string keyName (int tonic, bool minor);          // "A minor"
std::string keyShort (int tonic, bool minor);         // "Am"
std::string keyNameRu (int tonic, bool minor);        // "ля минор"
int relativeTonic (int tonic, bool minor);            // A minor -> C
std::string camelot (int tonic, bool minor);          // "8A"
std::vector<int> scaleNotes (int tonic, bool minor);  // натуральная гамма

//==============================================================================
class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate);
    void reset();

    // Моно-сигнал на исходной частоте дискретизации.
    void process (const float* mono, int n);

    KeyResult computeKey() const;
    TempoResult computeTempo() const;

    double secondsAnalyzed() const;   // секунды звука (без тишины)

    // Настройки (для экспериментов в тесте)
    struct Options
    {
        bool hpss = true;
        int harmonics = 4;
        int profileMask = 0xff;
        float minFreq = 40.0f, maxFreq = 3500.0f;
    };
    // Диапазон, к которому приводится темп: [rangeLo, 2*rangeLo).
    // 82 -> бум-бэп 82–100 остаётся как есть, трэп/дрилл 130–163 тоже.
    double rangeLo = 82.0;
    Options options;

private:
    void processInternal (const float* x, int n);
    void keyFrame();
    void onsetFrame();
    void closeSegment();

    Decimator dec;
    double fs = 11025.0;
    std::vector<float> scratch;

    // --- тональность ---
    int kN = 8192, kHop = 1024;
    std::unique_ptr<FFT> kFft;
    std::vector<float> kBuf, kWin, kMag;
    static constexpr int hpssK = 9;           // кадров для медианы по времени
    std::vector<std::vector<float>> kHist;    // история спектров
    std::vector<uint8_t> kHistSilent;
    int kHistCount = 0, kHistPos = 0;
    std::array<double, 120> pcHist {};        // 10 центов на ячейку
    long long keyFrames = 0;

    // --- темп ---
    int oN = 512, oHop = 64;
    std::unique_ptr<FFT> oFft;
    std::vector<float> oBuf, oWin, oMag, oPrevLog;
    bool oHavePrev = false;
    double frameRate = 172.0;
    std::vector<std::vector<float>> segments;
    std::vector<float> current;
    int silentRun = 0;
    long long activeFrames = 0;
};

} // namespace kbd
