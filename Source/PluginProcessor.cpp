#include "PluginProcessor.h"
#include "PluginEditor.h"

KeyBpmProcessor::KeyBpmProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("KeyBPM analysis")
{
    formatManager.registerBasicFormats();
    fifoData.resize ((size_t) fifoSize);
    pendingRate = 44100.0;
    startThread (juce::Thread::Priority::low);
}

KeyBpmProcessor::~KeyBpmProcessor()
{
    ++fileJobId;
    filePool.removeAllJobs (true, 5000);
    stopThread (3000);
}

bool KeyBpmProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void KeyBpmProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    monoScratch.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.0f);
    if (sampleRate > 0 && std::abs (sampleRate - pendingRate.load()) > 0.5)
        pendingRate = sampleRate;
}

void KeyBpmProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Звук проходит без изменений
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    if (frozen) return;

    const int numCh = juce::jmax (1, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    const int chunk = (int) monoScratch.size();
    if (chunk == 0) return;

    for (int start = 0; start < n; start += chunk)
    {
        const int m = juce::jmin (chunk, n - start);
        const float gain = 1.0f / (float) numCh;
        for (int i = 0; i < m; ++i)
        {
            float s = 0;
            for (int ch = 0; ch < numCh && ch < buffer.getNumChannels(); ++ch)
                s += buffer.getReadPointer (ch)[start + i];
            monoScratch[(size_t) i] = s * gain;
        }

        // Если поток анализа не успевает, лишнее просто пропускаем
        const auto scope = fifo.write (m);
        if (scope.blockSize1 > 0)
            std::copy (monoScratch.begin(), monoScratch.begin() + scope.blockSize1, fifoData.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (monoScratch.begin() + scope.blockSize1, monoScratch.begin() + scope.blockSize1 + scope.blockSize2,
                       fifoData.begin() + scope.startIndex2);
    }
}

//==============================================================================
void KeyBpmProcessor::run()
{
    double preparedRate = 0;
    std::vector<float> local (16384);
    juce::uint32 lastKey = 0, lastTempo = 0;
    bool dirty = false;

    while (! threadShouldExit())
    {
        const double rate = pendingRate.load();
        if (rate > 0 && std::abs (rate - preparedRate) > 0.5)
        {
            live.prepare (rate);
            preparedRate = rate;
            fifo.read (fifo.getNumReady()); // выбросить старое
            dirty = true;
        }

        if (resetRequested.exchange (false))
        {
            live.reset();
            fifo.read (fifo.getNumReady());
            dirty = true;
        }

        int got = 0;
        while (fifo.getNumReady() > 0 && ! threadShouldExit())
        {
            const auto scope = fifo.read (juce::jmin ((int) local.size(), fifo.getNumReady()));
            if (scope.blockSize1 > 0)
                std::copy (fifoData.begin() + scope.startIndex1, fifoData.begin() + scope.startIndex1 + scope.blockSize1, local.begin());
            if (scope.blockSize2 > 0)
                std::copy (fifoData.begin() + scope.startIndex2, fifoData.begin() + scope.startIndex2 + scope.blockSize2,
                           local.begin() + scope.blockSize1);
            const int m = scope.blockSize1 + scope.blockSize2;
            live.process (local.data(), m);
            got += m;
        }
        if (got > 0) dirty = true;

        const auto now = juce::Time::getMillisecondCounter();
        if (dirty && now - lastKey > 300)
        {
            lastKey = now;
            const bool doTempo = now - lastTempo > 900;
            if (doTempo) lastTempo = now;

            if (! fileMode)
            {
                const auto k = live.computeKey();
                kbd::TempoResult t;
                bool haveTempo = false;
                if (doTempo) { t = live.computeTempo(); haveTempo = true; }

                const juce::SpinLock::ScopedLockType sl (displayLock);
                auto& d = display;
                const bool hadData = d.restored && live.secondsAnalyzed() < 0.5;
                if (! hadData)
                {
                    d.restored = false;
                    d.fromFile = false;
                    d.fileName = {};
                    d.error = {};
                    d.keyValid = k.valid;
                    d.tonic = k.tonic;
                    d.minor = k.minor;
                    d.altTonic = k.altTonic;
                    d.altMinor = k.altMinor;
                    d.keyConfidence = k.confidence;
                    d.tuningCents = k.tuningCents;
                    d.chroma = k.chroma;
                    d.seconds = live.secondsAnalyzed();
                    if (haveTempo)
                    {
                        d.bpmValid = t.valid;
                        d.bpm = t.bpm;
                        d.bpmConfidence = t.confidence;
                    }
                }
            }
            dirty = doTempo ? false : dirty;
        }

        wait (40);
    }
}

void KeyBpmProcessor::resetLive()
{
    ++fileJobId;   // отменяет анализ файла, если он идёт
    fileMode = false;
    resetRequested = true;
    const juce::SpinLock::ScopedLockType sl (displayLock);
    display = DetectorDisplay();
}

void KeyBpmProcessor::analyseFile (const juce::File& file)
{
    const int myId = ++fileJobId;
    fileMode = true;
    {
        const juce::SpinLock::ScopedLockType sl (displayLock);
        display = DetectorDisplay();
        display.fromFile = true;
        display.fileName = file.getFileName();
        display.fileProgress = 0;
    }

    filePool.addJob ([this, file, myId]
    {
        auto setError = [this, myId] (const juce::String& msg) {
            if (fileJobId != myId) return;
            const juce::SpinLock::ScopedLockType sl (displayLock);
            display.error = msg;
            display.fileProgress = -1;
        };

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        {
            setError (juce::String::fromUTF8 ("Не удалось открыть файл"));
            return;
        }

        kbd::Analyzer a;
        a.prepare (reader->sampleRate);

        const int block = 65536;
        const int numCh = (int) juce::jmax (1u, reader->numChannels);
        juce::AudioBuffer<float> buf (numCh, block);
        std::vector<float> mono ((size_t) block);
        const auto total = reader->lengthInSamples;

        for (juce::int64 pos = 0; pos < total; pos += block)
        {
            if (fileJobId != myId) return;

            const int m = (int) juce::jmin ((juce::int64) block, total - pos);
            reader->read (&buf, 0, m, pos, true, true);
            for (int i = 0; i < m; ++i)
            {
                float s = 0;
                for (int ch = 0; ch < numCh; ++ch) s += buf.getReadPointer (ch)[i];
                mono[(size_t) i] = s / (float) numCh;
            }
            a.process (mono.data(), m);

            const juce::SpinLock::ScopedLockType sl (displayLock);
            display.fileProgress = (float) ((double) (pos + m) / (double) total);
        }

        const auto k = a.computeKey();
        const auto t = a.computeTempo();
        if (fileJobId != myId) return;

        const juce::SpinLock::ScopedLockType sl (displayLock);
        auto& d = display;
        d.keyValid = k.valid;
        d.tonic = k.tonic;
        d.minor = k.minor;
        d.altTonic = k.altTonic;
        d.altMinor = k.altMinor;
        d.keyConfidence = k.confidence;
        d.tuningCents = k.tuningCents;
        d.chroma = k.chroma;
        d.bpmValid = t.valid;
        d.bpm = t.bpm;
        d.bpmConfidence = t.confidence;
        d.seconds = a.secondsAnalyzed();
        d.fileProgress = -1;
        if (! k.valid && ! t.valid)
            d.error = juce::String::fromUTF8 ("В файле почти нет звука");
        return;
    });
}

DetectorDisplay KeyBpmProcessor::getDisplay() const
{
    const juce::SpinLock::ScopedLockType sl (displayLock);
    return display;
}

//==============================================================================
// Сохраняем последний результат в проект FL, чтобы он был виден после открытия
void KeyBpmProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto d = getDisplay();
    juce::XmlElement xml ("KeyBpmDetector");
    xml.setAttribute ("frozen", frozen.load());
    xml.setAttribute ("keyValid", d.keyValid);
    xml.setAttribute ("tonic", d.tonic);
    xml.setAttribute ("minor", d.minor);
    xml.setAttribute ("altTonic", d.altTonic);
    xml.setAttribute ("altMinor", d.altMinor);
    xml.setAttribute ("keyConf", d.keyConfidence);
    xml.setAttribute ("tuning", d.tuningCents);
    xml.setAttribute ("bpmValid", d.bpmValid);
    xml.setAttribute ("bpm", d.bpm);
    xml.setAttribute ("bpmConf", d.bpmConfidence);
    xml.setAttribute ("seconds", d.seconds);
    xml.setAttribute ("fromFile", d.fromFile);
    xml.setAttribute ("fileName", d.fileName);
    juce::String chroma;
    for (float c : d.chroma) chroma << juce::String (c, 3) << " ";
    xml.setAttribute ("chroma", chroma.trim());
    copyXmlToBinary (xml, destData);
}

void KeyBpmProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("KeyBpmDetector")) return;

    frozen = xml->getBoolAttribute ("frozen", false);

    DetectorDisplay d;
    d.keyValid = xml->getBoolAttribute ("keyValid");
    d.tonic = juce::jlimit (0, 11, xml->getIntAttribute ("tonic"));
    d.minor = xml->getBoolAttribute ("minor");
    d.altTonic = juce::jlimit (0, 11, xml->getIntAttribute ("altTonic"));
    d.altMinor = xml->getBoolAttribute ("altMinor");
    d.keyConfidence = (float) xml->getDoubleAttribute ("keyConf");
    d.tuningCents = (float) xml->getDoubleAttribute ("tuning");
    d.bpmValid = xml->getBoolAttribute ("bpmValid");
    d.bpm = xml->getDoubleAttribute ("bpm");
    d.bpmConfidence = (float) xml->getDoubleAttribute ("bpmConf");
    d.seconds = xml->getDoubleAttribute ("seconds");
    d.fromFile = xml->getBoolAttribute ("fromFile");
    d.fileName = xml->getStringAttribute ("fileName");
    auto parts = juce::StringArray::fromTokens (xml->getStringAttribute ("chroma"), " ", "");
    for (int i = 0; i < 12 && i < parts.size(); ++i) d.chroma[(size_t) i] = parts[i].getFloatValue();
    d.restored = d.keyValid || d.bpmValid;

    const juce::SpinLock::ScopedLockType sl (displayLock);
    display = d;
}

juce::AudioProcessorEditor* KeyBpmProcessor::createEditor() { return new KeyBpmEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KeyBpmProcessor(); }
