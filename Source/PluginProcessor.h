#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "core/KeyBpmCore.h"

#include <array>
#include <atomic>

// То, что показывает окно плагина
struct DetectorDisplay
{
    bool keyValid = false;
    int tonic = 0;
    bool minor = true;
    int altTonic = 0;
    bool altMinor = true;
    float keyConfidence = 0;
    float tuningCents = 0;
    std::array<float, 12> chroma {};

    bool bpmValid = false;
    double bpm = 0;
    float bpmConfidence = 0;

    double seconds = 0;          // сколько звука проанализировано
    bool fromFile = false;
    bool restored = false;       // восстановлено из сохранённого проекта
    juce::String fileName;
    float fileProgress = -1;     // 0..1 во время анализа файла, иначе -1
    juce::String error;
};

class KeyBpmProcessor : public juce::AudioProcessor,
                        private juce::Thread
{
public:
    KeyBpmProcessor();
    ~KeyBpmProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- для окна ---
    DetectorDisplay getDisplay() const;
    void resetLive();                         // сброс и возврат к анализу входа
    void analyseFile (const juce::File& file);
    void setFrozen (bool f) { frozen = f; }
    bool isFrozen() const { return frozen; }
    double getHostBpm() const { return hostBpm.load(); }

    juce::AudioFormatManager formatManager;

private:
    void run() override;
    void publishLive();

    // Аудиопоток -> поток анализа
    static constexpr int fifoSize = 1 << 19;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::vector<float> monoScratch;

    std::atomic<double> pendingRate { 0.0 };
    std::atomic<bool> resetRequested { false };
    std::atomic<bool> frozen { false };
    std::atomic<bool> fileMode { false };
    std::atomic<double> hostBpm { 0.0 };

    kbd::Analyzer live;          // используется только потоком анализа

    mutable juce::SpinLock displayLock;
    DetectorDisplay display;

    // Анализ файла
    juce::ThreadPool filePool { 1 };
    std::atomic<int> fileJobId { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyBpmProcessor)
};
