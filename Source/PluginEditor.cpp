#include "PluginEditor.h"

namespace
{
const juce::Colour cBg      { 0xff121218 };
const juce::Colour cCard    { 0xff1c1c26 };
const juce::Colour cBorder  { 0xff2c2c3a };
const juce::Colour cText    { 0xffeeeef5 };
const juce::Colour cGrey    { 0xff8a8aa0 };
const juce::Colour cDim     { 0xff3a3a4c };
const juce::Colour cKey     { 0xffa78bfa };
const juce::Colour cRel     { 0xff7dd3fc };
const juce::Colour cBpm     { 0xff34d399 };
const juce::Colour cWarn    { 0xfffbbf24 };

juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }
juce::String ru (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String formatBpm (double bpm)
{
    if (std::abs (bpm - std::round (bpm)) < 0.05) return juce::String ((int) std::round (bpm));
    return juce::String (bpm, 1);
}

void drawConfidence (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour accent, const juce::String& extra)
{
    const int pct = juce::roundToInt (juce::jlimit (0.0f, 1.0f, value) * 100.0f);
    const auto c = value >= 0.6f ? accent : (value >= 0.3f ? cWarn : juce::Colour (0xfff87171));

    g.setFont (font (12.0f));
    g.setColour (cGrey);
    auto text = r.removeFromTop (16.0f);
    g.drawText (ru ("Уверенность ") + juce::String (pct) + "%" + extra, text, juce::Justification::centredLeft);

    auto bar = r.removeFromTop (6.0f);
    g.setColour (cDim);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (c);
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, value))), 3.0f);
}

void cardBackground (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (cCard);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (cBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}
} // namespace

//==============================================================================
KeyBpmEditor::KeyBpmEditor (KeyBpmProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262633));
    lnf.setColour (juce::TextButton::buttonOnColourId, cKey.withAlpha (0.85f));
    lnf.setColour (juce::TextButton::textColourOffId, cText);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::outlineColourId, cBorder);
    setLookAndFeel (&lnf);

    fileButton.setButtonText (ru ("Открыть файл"));
    fileButton.setTooltip (ru ("Проанализировать аудиофайл целиком"));
    fileButton.onClick = [this] { openFileChooser(); };

    freezeButton.setButtonText (ru ("Заморозить"));
    freezeButton.setClickingTogglesState (true);
    freezeButton.setToggleState (proc.isFrozen(), juce::dontSendNotification);
    freezeButton.onClick = [this] { proc.setFrozen (freezeButton.getToggleState()); };

    resetButton.setButtonText (ru ("Сброс"));
    resetButton.onClick = [this] {
        freezeButton.setToggleState (false, juce::dontSendNotification);
        proc.setFrozen (false);
        proc.resetLive();
    };

    for (auto* b : { &fileButton, &freezeButton, &resetButton })
        addAndMakeVisible (b);

    setSize (620, 410);
    d = proc.getDisplay();
    startTimerHz (15);
}

KeyBpmEditor::~KeyBpmEditor()
{
    setLookAndFeel (nullptr);
}

void KeyBpmEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (56).reduced (16, 14);
    resetButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (8);
    freezeButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (8);
    fileButton.setBounds (header.removeFromRight (120));

    auto body = getLocalBounds().toFloat().withTrimmedTop (60.0f).reduced (16.0f, 0.0f);
    footer = body.removeFromBottom (44.0f);
    body.removeFromBottom (8.0f);
    bpmCard = body.removeFromRight (200.0f);
    body.removeFromRight (12.0f);
    keyCard = body;
}

void KeyBpmEditor::timerCallback()
{
    d = proc.getDisplay();
    if (freezeButton.getToggleState() != proc.isFrozen())
        freezeButton.setToggleState (proc.isFrozen(), juce::dontSendNotification);
    repaint();
}

//==============================================================================
void KeyBpmEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);

    // Заголовок
    g.setColour (cText);
    g.setFont (font (20.0f, true));
    g.drawText ("KeyBPM", 18, 12, 90, 30, juce::Justification::centredLeft);
    g.setColour (cKey);
    g.drawText ("Detector", 96, 12, 120, 30, juce::Justification::centredLeft);
    g.setColour (cGrey);
    g.setFont (font (11.0f));
    g.drawText ("by 103 MUSIC", 18, 36, 120, 14, juce::Justification::centredLeft);

    paintKeyCard (g, keyCard);
    paintBpmCard (g, bpmCard);
    paintFooter (g, footer);

    if (dragOver)
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f);
        g.setColour (cBg.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        juce::Path p;
        p.addRoundedRectangle (r, 14.0f);
        juce::Path dashed;
        const float dashes[] = { 10.0f, 6.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (cKey);
        g.fillPath (dashed);
        g.setFont (font (22.0f, true));
        g.drawText (ru ("Отпусти файл — проанализирую"), r, juce::Justification::centred);
    }
}

void KeyBpmEditor::paintKeyCard (juce::Graphics& g, juce::Rectangle<float> card)
{
    cardBackground (g, card);
    auto r = card.reduced (16.0f, 12.0f);

    auto top = r.removeFromTop (16.0f);
    g.setFont (font (11.0f, true));
    g.setColour (cGrey);
    g.drawText (ru ("ТОНАЛЬНОСТЬ"), top, juce::Justification::centredLeft);

    const bool working = d.fileProgress >= 0.0f;

    if (! d.keyValid)
    {
        g.setColour (cDim);
        g.setFont (font (46.0f, true));
        g.drawText (working ? ru ("…") : ru ("—"), r.removeFromTop (70.0f), juce::Justification::centredLeft);
        g.setColour (cGrey);
        g.setFont (font (14.0f));
        g.drawFittedText (working ? ru ("Анализирую файл…")
                                  : ru ("Включи бит, сэмпл или трек в FL —\nили перетащи сюда аудиофайл."),
                          r.removeFromTop (44.0f).toNearestInt(), juce::Justification::topLeft, 3);
        return;
    }

    // Камелот
    {
        const auto cam = ru (kbd::camelot (d.tonic, d.minor));
        auto badge = top.removeFromRight (40.0f).withSizeKeepingCentre (40.0f, 18.0f).translated (0, 2);
        g.setColour (cDim);
        g.fillRoundedRectangle (badge, 9.0f);
        g.setColour (cText);
        g.setFont (font (11.0f, true));
        g.drawText (cam, badge, juce::Justification::centred);
        g.setColour (cGrey);
        g.setFont (font (10.0f));
        top.removeFromRight (8.0f);
        g.drawText ("Camelot", top.removeFromRight (52.0f), juce::Justification::centredRight);
    }

    // Основная тональность
    g.setColour (cKey);
    g.setFont (font (44.0f, true));
    g.drawText (ru (kbd::keyName (d.tonic, d.minor)), r.removeFromTop (54.0f), juce::Justification::centredLeft);

    auto sub = r.removeFromTop (20.0f);
    g.setColour (cText.withAlpha (0.8f));
    g.setFont (font (15.0f));
    g.drawText (ru (kbd::keyNameRu (d.tonic, d.minor)) + ru ("  ·  ") + ru (kbd::keyShort (d.tonic, d.minor)),
                sub, juce::Justification::centredLeft);
    if (std::abs (d.tuningCents) >= 8.0f)
    {
        g.setColour (cWarn);
        g.setFont (font (12.0f));
        g.drawText (ru ("строй ") + (d.tuningCents > 0 ? "+" : "") + juce::String (juce::roundToInt (d.tuningCents)) + ru (" ц"),
                    sub, juce::Justification::centredRight);
    }

    r.removeFromTop (10.0f);

    // Параллельная
    const int rel = kbd::relativeTonic (d.tonic, d.minor);
    g.setColour (cGrey);
    g.setFont (font (11.0f, true));
    g.drawText (ru ("ПАРАЛЛЕЛЬНАЯ"), r.removeFromTop (14.0f), juce::Justification::centredLeft);
    auto relRow = r.removeFromTop (30.0f);
    g.setColour (cRel);
    g.setFont (font (24.0f, true));
    const auto relName = ru (kbd::keyName (rel, ! d.minor));
    g.drawText (relName, relRow, juce::Justification::centredLeft);
    const float relW = juce::GlyphArrangement::getStringWidth (font (24.0f, true), relName);
    g.setColour (cText.withAlpha (0.7f));
    g.setFont (font (14.0f));
    g.drawText (ru ("   ") + ru (kbd::keyNameRu (rel, ! d.minor)) + ru ("  ·  ") + ru (kbd::keyShort (rel, ! d.minor)),
                relRow.withTrimmedLeft (relW), juce::Justification::centredLeft);

    r.removeFromTop (8.0f);

    // Ноты лада + хромаграмма
    const auto notes = kbd::scaleNotes (d.tonic, d.minor);
    juce::String noteList;
    for (int n : notes) noteList << ru (kbd::noteName (n)) << "  ";
    g.setColour (cGrey);
    g.setFont (font (12.0f));
    auto notesRow = r.removeFromTop (16.0f);
    g.drawText (ru ("Ноты лада:  "), notesRow, juce::Justification::centredLeft);
    g.setColour (cText);
    g.setFont (font (13.0f, true));
    g.drawText (noteList.trim(), notesRow.withTrimmedLeft (74.0f), juce::Justification::centredLeft);

    r.removeFromTop (6.0f);
    auto bars = r.removeFromTop (54.0f);
    const float bw = bars.getWidth() / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        auto col = juce::Rectangle<float> (bars.getX() + i * bw, bars.getY(), bw, bars.getHeight()).reduced (2.5f, 0);
        auto label = col.removeFromBottom (14.0f);
        const bool inScale = std::find (notes.begin(), notes.end(), i) != notes.end();
        const float v = juce::jlimit (0.0f, 1.0f, d.chroma[(size_t) i]);
        g.setColour (cDim.withAlpha (0.6f));
        g.fillRoundedRectangle (col, 3.0f);
        auto fill = col.withTrimmedTop (col.getHeight() * (1.0f - v));
        g.setColour (i == d.tonic ? cKey : (inScale ? cKey.withAlpha (0.55f) : cGrey.withAlpha (0.45f)));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (i == d.tonic ? cText : (inScale ? cText.withAlpha (0.8f) : cGrey));
        g.setFont (font (10.0f, i == d.tonic));
        g.drawText (ru (kbd::noteName (i)), label, juce::Justification::centredBottom);
    }

    r.removeFromTop (10.0f);
    juce::String extra;
    if (d.keyConfidence < 0.8f)
        extra << ru ("   ·   ещё возможно: ") << ru (kbd::keyName (d.altTonic, d.altMinor));
    if (! d.fromFile && d.seconds < 12.0) extra << ru ("   ·   уточняется…");
    drawConfidence (g, r.removeFromTop (24.0f), d.keyConfidence, cKey, extra);
}

void KeyBpmEditor::paintBpmCard (juce::Graphics& g, juce::Rectangle<float> card)
{
    cardBackground (g, card);
    auto r = card.reduced (16.0f, 12.0f);

    g.setFont (font (11.0f, true));
    g.setColour (cGrey);
    g.drawText (ru ("ТЕМП"), r.removeFromTop (16.0f), juce::Justification::centredLeft);

    if (! d.bpmValid)
    {
        g.setColour (cDim);
        g.setFont (font (56.0f, true));
        g.drawText (ru ("—"), r.removeFromTop (76.0f), juce::Justification::centredLeft);
        g.setColour (cGrey);
        g.setFont (font (13.0f));
        g.drawFittedText (ru ("Нужно несколько\nсекунд звука"), r.removeFromTop (40.0f).toNearestInt(),
                          juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (cBpm);
        g.setFont (font (60.0f, true));
        g.drawText (formatBpm (d.bpm), r.removeFromTop (70.0f), juce::Justification::centredLeft);
        g.setColour (cText.withAlpha (0.8f));
        g.setFont (font (14.0f, true));
        g.drawText ("BPM", r.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setColour (cGrey);
        g.setFont (font (11.0f));
        g.drawText (juce::String (d.bpm, 2), r.removeFromTop (14.0f), juce::Justification::centredLeft);

        r.removeFromTop (12.0f);
        g.setColour (cGrey);
        g.setFont (font (11.0f, true));
        g.drawText (ru ("ЕСЛИ СЧИТАТЬ ИНАЧЕ"), r.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.setColour (cText.withAlpha (0.85f));
        g.setFont (font (15.0f));
        g.drawText (ru ("÷2  ") + formatBpm (d.bpm / 2.0) + ru ("     ×2  ") + formatBpm (d.bpm * 2.0),
                    r.removeFromTop (22.0f), juce::Justification::centredLeft);
    }

    // Темп проекта FL
    const double host = proc.getHostBpm();
    auto bottom = card.reduced (16.0f, 12.0f);
    auto conf = bottom.removeFromBottom (24.0f);
    bottom.removeFromBottom (10.0f);
    auto hostRow = bottom.removeFromBottom (18.0f);
    if (host > 0)
    {
        g.setFont (font (12.0f));
        bool match = false;
        if (d.bpmValid)
            for (double m : { 1.0, 0.5, 2.0 })
                if (std::abs (d.bpm * m - host) < 0.25) match = true;
        g.setColour (match ? cBpm : cGrey);
        g.drawText (ru ("Проект: ") + formatBpm (host) + (match ? ru ("  ✓ совпадает") : juce::String()),
                    hostRow, juce::Justification::centredLeft);
    }

    if (d.bpmValid)
        drawConfidence (g, conf, d.bpmConfidence, cBpm, {});
}

void KeyBpmEditor::paintFooter (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (cCard);
    g.fillRoundedRectangle (r, 10.0f);
    r = r.reduced (14.0f, 0.0f);

    juce::String status;
    juce::Colour dot = cBpm;
    if (d.error.isNotEmpty())
    {
        status = d.error;
        dot = juce::Colour (0xfff87171);
    }
    else if (d.fileProgress >= 0.0f)
    {
        status = ru ("Анализ файла ") + d.fileName + "  " + juce::String (juce::roundToInt (d.fileProgress * 100.0f)) + "%";
        dot = cWarn;
    }
    else if (d.fromFile)
    {
        status = ru ("Файл: ") + d.fileName;
        dot = cRel;
    }
    else if (proc.isFrozen())
    {
        status = ru ("Заморожено — результат не меняется");
        dot = cWarn;
    }
    else if (d.restored)
    {
        status = ru ("Сохранённый результат · включи звук, чтобы обновить");
        dot = cGrey;
    }
    else
    {
        status = ru ("Слушаю вход") + (d.seconds > 0.5 ? ru ("  ·  ") + juce::String (juce::roundToInt (d.seconds)) + ru (" с звука") : juce::String());
    }

    g.setColour (dot);
    g.fillEllipse (r.getX(), r.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (cText.withAlpha (0.9f));
    g.setFont (font (13.0f));
    g.drawText (status, r.withTrimmedLeft (16.0f), juce::Justification::centredLeft);

    g.setColour (cGrey);
    g.setFont (font (11.0f));
    g.drawText (ru ("Перетащи сюда wav / mp3 / flac"), r, juce::Justification::centredRight);
}

//==============================================================================
void KeyBpmEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> (ru ("Выбери аудиофайл"), juce::File(),
                                                   proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  freezeButton.setToggleState (false, juce::dontSendNotification);
                                  proc.setFrozen (false);
                                  proc.analyseFile (f);
                              }
                          });
}

bool KeyBpmEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void KeyBpmEditor::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint(); }
void KeyBpmEditor::fileDragExit (const juce::StringArray&) { dragOver = false; repaint(); }

void KeyBpmEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& path : files)
    {
        juce::File f (path);
        if (proc.formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr)
        {
            freezeButton.setToggleState (false, juce::dontSendNotification);
            proc.setFrozen (false);
            proc.analyseFile (f);
            break;
        }
    }
    repaint();
}
