// kbd_snapshot out.png [file.wav] [--live]
// Без файла — пустое окно. С файлом — анализ через analyseFile (или через processBlock с --live).
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<KeyBpmProcessor>();
    proc->setRateAndBufferSizeDetails (44100.0, 512);
    proc->prepareToPlay (44100.0, 512);

    const bool liveMode = argc > 3 && juce::String (argv[3]) == "--live";
    if (argc > 2)
    {
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        if (liveMode)
        {
            std::unique_ptr<juce::AudioFormatReader> r (proc->formatManager.createReaderFor (f));
            if (r == nullptr) return 2;
            proc->prepareToPlay (r->sampleRate, 512);
            juce::Thread::sleep (300);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += 512)
            {
                buf.clear();
                r->read (&buf, 0, 512, pos, true, true);
                proc->processBlock (buf, midi);
                if ((pos / 512) % 64 == 0) juce::Thread::sleep (5); // даём потоку анализа время
            }
            juce::Thread::sleep (2500);
        }
        else
        {
            proc->analyseFile (f);
            for (int i = 0; i < 600; ++i)
            {
                juce::Thread::sleep (50);
                auto d = proc->getDisplay();
                if (d.fileProgress < 0 && (d.keyValid || d.error.isNotEmpty())) break;
            }
        }
    }

    auto d = proc->getDisplay();
    std::printf ("keyValid=%d key=%s bpmValid=%d bpm=%.3f conf=%.2f/%.2f seconds=%.1f error=%s\n", d.keyValid,
                 kbd::keyShort (d.tonic, d.minor).c_str(), d.bpmValid, d.bpm, d.keyConfidence, d.bpmConfidence,
                 d.seconds, d.error.toRawUTF8());

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    proc.reset();
    return 0;
}
