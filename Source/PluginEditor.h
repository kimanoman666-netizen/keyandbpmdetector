#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class KeyBpmEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit KeyBpmEditor (KeyBpmProcessor&);
    ~KeyBpmEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void openFileChooser();

    void paintKeyCard (juce::Graphics&, juce::Rectangle<float>);
    void paintBpmCard (juce::Graphics&, juce::Rectangle<float>);
    void paintFooter (juce::Graphics&, juce::Rectangle<float>);

    KeyBpmProcessor& proc;
    DetectorDisplay d;

    juce::LookAndFeel_V4 lnf;
    juce::TextButton fileButton, freezeButton, resetButton;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;

    juce::Rectangle<float> keyCard, bpmCard, footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyBpmEditor)
};
