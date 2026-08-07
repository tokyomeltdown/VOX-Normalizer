/*
    This file is part of VOX Normalizer.
    Copyright (C) 2026 Ryo Yoneya (tokyomeltdown)

    VOX Normalizer is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    VOX Normalizer is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "PluginEditor.h"

// ---- Colour palette (defined once in the vox namespace in VoxLookAndFeel.h) ----
static const juce::Colour colBg        = vox::bg;        // Apple system dark (near black)
static const juce::Colour colPanel     = vox::panel;     // one step lighter dark grey
static const juce::Colour colAccent    = vox::accent;    // warm coral
static const juce::Colour colText      = vox::text;      // pure white
static const juce::Colour colSubText   = vox::subText;   // medium grey
static const juce::Colour colDropHover = vox::dropHover; // accent with alpha

// ---- Forcing stereo output (works around CoreAudio starting up in mono) ----
//
// Cause: JUCE Standalone sometimes initialises CoreAudio in mono at startup.
// This reproduces in code what fixes it by hand: unticking MON L+R in the
// settings and ticking it again.
// - Passing the same setup back does nothing, because JUCE decides nothing
// changed and skips the reinitialise.
// - So force a channel change in two steps: L only, wait a moment, then L+R.
#if JucePlugin_Build_Standalone
 // StandalonePluginHolder is not visible through JuceHeader.h, so include it directly
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace {

    // Step 2: reopen in stereo after closeAudioDevice (called 500ms later)
    void applyStereoPatchStep2()
    {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            // Remove the feedback-loop warning banner.
            // There is no input bus and no input channels, so there is no feedback risk.
            holder->shouldMuteInput.setValue (false);

            auto& dm    = holder->deviceManager;
            auto  setup = dm.getAudioDeviceSetup();
            // Do not open an input device at all (output only).
            // NOTE: inputChannels.clear() alone leaves the input device open, and on another
            // Mac, where the output device differs from the input device, the audio callback
            // never runs and the output goes silent too. Clearing inputDeviceName avoids that.
            setup.inputDeviceName = {};
            setup.inputChannels.clear();
            setup.useDefaultInputChannels = false;
            // Force stereo output
            setup.outputChannels.clear();
            setup.outputChannels.setBit (0);      // L
            setup.outputChannels.setBit (1);      // R
            setup.useDefaultOutputChannels = false;
            dm.setAudioDeviceSetup (setup, true);
        }
    }

    // Step 1: close the device completely to fully reset CoreAudio.
    // setAudioDeviceSetup on its own can be treated as no change, so cut the
    // session with closeAudioDevice() before reopening.
    void forceStereoPatch()
    {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            holder->deviceManager.closeAudioDevice();
        }

        // Reopen in stereo after 500ms
        juce::Timer::callAfterDelay (500, applyStereoPatchStep2);
    }

} // anonymous namespace
#endif

VUClipGainNormalizerEditor::VUClipGainNormalizerEditor (VUClipGainNormalizerProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // v1.2: one extra row for the Peak Ceiling slider, then the Step 5-2 card layout
    // tightened the spacing again, so 726 became 722.
    // *** Must match setContentComponentSize in parentHierarchyChanged().
    setSize (620, 722);
    setWantsKeyboardFocus (true);

    // v1.2: apply the modern LookAndFeel to the whole Editor; children inherit it
    setLookAndFeel (&voxLAF);

#if JucePlugin_Build_Standalone
    // Apply the stereo patch after 500ms
    // (deliberately generous, so CoreAudio has finished initialising the device first)
    juce::Timer::callAfterDelay (500, forceStereoPatch);
#endif

    // Play button: a LookAndFeel that draws the shapes directly
    playPauseButton.setLookAndFeel (&symbolButtonLAF);

    addAndMakeVisible (waveformView);

    // NOTE: hiding things before a file is loaded happens after every addAndMakeVisible().
    // v1.1 called setVisible(false) here, and the later addAndMakeVisible() calls
    // overrode it, so Play / Original / Normalized showed even with nothing loaded.

    // Waveform click moves the playback position
    waveformView.onCursorClick = [this] (double normalizedPos)
    {
        double len = processorRef.getPlaybackLengthSeconds();
        if (len > 0.0)
        {
            processorRef.setPlaybackPositionSeconds (normalizedPos * len);
            waveformView.setPlaybackCursor (normalizedPos);
        }
    };

    // Boundary drag finished: update the clips, then re-analyse
    waveformView.onBoundaryDragged = [this] (int clipIndex, bool isStart, int64_t newSamplePos)
    {
        processorRef.moveClipBoundary (clipIndex, isStart, newSamplePos);
        waveformView.setClips (&processorRef.getVirtualClips());  // update first, so the zoom is preserved
        runAnalysis();  // analyzeClips, the normalizedAudioBuf rebuild and setAnalyses in one go
    };

    // Open button
    addAndMakeVisible (openButton);
    openButton.setColour (juce::TextButton::buttonColourId,  colPanel);
    openButton.setColour (juce::TextButton::textColourOffId, colAccent);
    openButton.onClick = [this] { openFileDialog(); };


    // File name
    fileNameLabel.setFont (juce::Font (17.0f, juce::Font::bold));
    fileNameLabel.setColour (juce::Label::textColourId, colText);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);

    // File info
    fileInfoLabel.setFont (juce::Font (12.0f));
    fileInfoLabel.setColour (juce::Label::textColourId, colSubText);
    fileInfoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileInfoLabel);

    // MP3 notice
    mp3NoteLabel.setFont (juce::Font (11.0f));
    mp3NoteLabel.setColour (juce::Label::textColourId, juce::Colour (0xffefb854));
    mp3NoteLabel.setText ("Note: MP3 will be exported as WAV.", juce::dontSendNotification);
    mp3NoteLabel.setVisible (false);
    addAndMakeVisible (mp3NoteLabel);

    // ---- Clip Detection section ----
    sectionDetectLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    sectionDetectLabel.setColour (juce::Label::textColourId, colAccent);
    addAndMakeVisible (sectionDetectLabel);

    // Whole file toggle (v1.2: macOS-style switch, label on the left)
    wholeFileToggle.setColour (juce::ToggleButton::textColourId, colSubText);
    wholeFileToggle.setTextOnLeft (true);
    wholeFileToggle.onClick = [this]
    {
        // v1.2: greyed out rather than hidden, so the card does not end up
        // with a large empty gap in it
        setDetectionControlsEnabled (! wholeFileToggle.getToggleState());
        runDetection();
        runAnalysis();   // *** Required: the clip layout changed, so the analysis must be redone.
                         // Without this the stale analysis survives and the export distorts (a v1.1 bug).
    };
    addAndMakeVisible (wholeFileToggle);

    // Silence Threshold slider
    threshLabel.setFont (juce::Font (12.0f));
    threshLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (threshLabel);

    threshSlider.setRange (-80.0, -30.0, 1.0);
    threshSlider.setValue (-50.0);
    threshSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    threshSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    threshSlider.setColour (juce::Slider::thumbColourId,           colAccent);
    threshSlider.setColour (juce::Slider::trackColourId,           colAccent.withAlpha (0.4f));
    threshSlider.setColour (juce::Slider::backgroundColourId,      colPanel);
    threshSlider.addListener (this);
    addAndMakeVisible (threshSlider);

    threshValLabel.setFont (juce::Font (12.0f));
    threshValLabel.setColour (juce::Label::textColourId, colText);
    threshValLabel.setJustificationType (juce::Justification::centredRight);
    threshValLabel.setText ("-50 dBFS", juce::dontSendNotification);
    addAndMakeVisible (threshValLabel);

    // Min. Silence Duration slider
    durLabel.setFont (juce::Font (12.0f));
    durLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (durLabel);

    durSlider.setRange (20.0, 3000.0, 10.0);
    durSlider.setValue (100.0);
    durSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    durSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    durSlider.setColour (juce::Slider::thumbColourId,           colAccent);
    durSlider.setColour (juce::Slider::trackColourId,           colAccent.withAlpha (0.4f));
    durSlider.setColour (juce::Slider::backgroundColourId,      colPanel);
    durSlider.addListener (this);
    addAndMakeVisible (durSlider);

    durValLabel.setFont (juce::Font (12.0f));
    durValLabel.setColour (juce::Label::textColourId, colText);
    durValLabel.setJustificationType (juce::Justification::centredRight);
    durValLabel.setText ("100 ms", juce::dontSendNotification);
    addAndMakeVisible (durValLabel);

    // Clip count
    clipCountLabel.setFont (juce::Font (14.0f, juce::Font::bold));
    clipCountLabel.setColour (juce::Label::textColourId, colText);
    clipCountLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (clipCountLabel);

    // ---- Normalize Settings section ----
    sectionNormLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    sectionNormLabel.setColour (juce::Label::textColourId, colAccent);
    addAndMakeVisible (sectionNormLabel);

    // Target Level slider
    targetLabel.setFont (juce::Font (12.0f));
    targetLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (targetLabel);

    targetSlider.setRange (-24.0, -12.0, 0.5);
    targetSlider.setValue (-18.0);
    targetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    targetSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    targetSlider.setColour (juce::Slider::thumbColourId,      colAccent);
    targetSlider.setColour (juce::Slider::trackColourId,      colAccent.withAlpha (0.4f));
    targetSlider.setColour (juce::Slider::backgroundColourId, colPanel);
    targetSlider.addListener (this);
    addAndMakeVisible (targetSlider);

    targetValLabel.setFont (juce::Font (12.0f));
    targetValLabel.setColour (juce::Label::textColourId, colText);
    targetValLabel.setJustificationType (juce::Justification::centredRight);
    targetValLabel.setText ("-18.0 dBFS", juce::dontSendNotification);
    addAndMakeVisible (targetValLabel);

    // Max Gain slider
    maxGainLabel.setFont (juce::Font (12.0f));
    maxGainLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (maxGainLabel);

    maxGainSlider.setRange (3.0, 24.0, 0.5);
    maxGainSlider.setValue (12.0);
    maxGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    maxGainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    maxGainSlider.setColour (juce::Slider::thumbColourId,      colAccent);
    maxGainSlider.setColour (juce::Slider::trackColourId,      colAccent.withAlpha (0.4f));
    maxGainSlider.setColour (juce::Slider::backgroundColourId, colPanel);
    maxGainSlider.addListener (this);
    addAndMakeVisible (maxGainSlider);

    maxGainValLabel.setFont (juce::Font (12.0f));
    maxGainValLabel.setColour (juce::Label::textColourId, colText);
    maxGainValLabel.setJustificationType (juce::Justification::centredRight);
    maxGainValLabel.setText ("+/- 12.0 dB", juce::dontSendNotification);
    addAndMakeVisible (maxGainValLabel);

    // Peak Ceiling slider (upper bound for the output peak, i.e. clipping protection)
    peakLabel.setFont (juce::Font (12.0f));
    peakLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (peakLabel);

    peakSlider.setRange (-6.0, 0.0, 0.1);
    peakSlider.setValue (-1.0);
    peakSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    peakSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    peakSlider.setColour (juce::Slider::thumbColourId,      colAccent);
    peakSlider.setColour (juce::Slider::trackColourId,      colAccent.withAlpha (0.4f));
    peakSlider.setColour (juce::Slider::backgroundColourId, colPanel);
    peakSlider.addListener (this);
    addAndMakeVisible (peakSlider);

    peakValLabel.setFont (juce::Font (12.0f));
    peakValLabel.setColour (juce::Label::textColourId, colText);
    peakValLabel.setJustificationType (juce::Justification::centredRight);
    peakValLabel.setText ("-1.0 dBFS", juce::dontSendNotification);
    addAndMakeVisible (peakValLabel);

    // ---- Preview controls ----
    playPauseButton.setColour (juce::TextButton::buttonColourId,  colAccent);
    playPauseButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playPauseButton.onClick = [this]
    {
        if (! processorRef.getFileInfo().isValid()) return;
        if (processorRef.isPreviewPlaying())
        {
            // Playing, so pause and keep the position
            processorRef.pausePreview();
            updatePlayButton();
            stopTimer();
        }
        else
        {
            // Stopped, so resume from the current position
            processorRef.startPreview (processorRef.isPreviewNormalized());
            updatePlayButton();
            startTimerHz (30);
        }
    };
    addAndMakeVisible (playPauseButton);

    // Original / Normalized (v1.2: segmented control)
    abControl.addSegment ("Original");
    abControl.addSegment ("Normalized");
    abControl.onChange = [this] (int index)
    {
        const bool normalized = (index == 1);
        processorRef.startPreview (normalized);
        waveformView.showNormalized (normalized);
        updatePlayButton();   // switch the icon to stop, since playback just started
        startTimerHz (30);
    };
    addAndMakeVisible (abControl);

    // Reflect the initial selection (Original)
    updateABButtons();

    // Save button
    saveButton.setColour (juce::TextButton::buttonColourId,  colAccent);
    saveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    saveButton.onClick = [this] { openSaveDialog(); };
    addAndMakeVisible (saveButton);

    // Status label
    saveStatusLabel.setFont (juce::Font (12.0f));
    saveStatusLabel.setColour (juce::Label::textColourId, colSubText);
    saveStatusLabel.setJustificationType (juce::Justification::centred);  // centred to line up with the Save button
    addAndMakeVisible (saveStatusLabel);

    // Clip list (TableListBox)
    clipListBox.setModel (&clipListModel);
    clipListBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0d0d1a));
    clipListBox.setColour (juce::ListBox::outlineColourId,    colPanel);
    clipListBox.setOutlineThickness (1);
    clipListBox.setRowHeight (22);

    auto& hdr = clipListBox.getHeader();
    hdr.addColumn ("#",       0, 40,  40,  40,  juce::TableHeaderComponent::notSortable);
    hdr.addColumn ("RMS",     1, 90,  70,  120, juce::TableHeaderComponent::notSortable);
    hdr.addColumn ("Gain",    2, 90,  70,  120, juce::TableHeaderComponent::notSortable);
    hdr.addColumn ("Status",  3, 60,  50,  80,  juce::TableHeaderComponent::notSortable);
    hdr.setColour (juce::TableHeaderComponent::backgroundColourId, colPanel);
    hdr.setColour (juce::TableHeaderComponent::textColourId,       colSubText);
    hdr.setColour (juce::TableHeaderComponent::outlineColourId,    colAccent.withAlpha (0.3f));

    addAndMakeVisible (clipListBox);

    // ---- Hide the waveform and preview controls until a file is loaded ----
    // (loadFileAndUpdate() shows them again on success)
    waveformView   .setVisible (false);
    playPauseButton.setVisible (false);
    abControl      .setVisible (false);
    mp3NoteLabel   .setVisible (false);   // v1.1 bug: the MP3 notice showed even with nothing loaded

    setDetectionControlsEnabled (true);   // Whole file starts off

    // ---- AAX AudioSuite UI ----
    const bool isAAX = (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone);

    // Title (AAX only)
    aaxTitleLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    aaxTitleLabel.setColour (juce::Label::textColourId, colSubText);
    aaxTitleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (aaxTitleLabel);

    // Mode label
    aaxModeLabel.setFont (juce::Font (12.0f));
    aaxModeLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (aaxModeLabel);

    // Analyze / Apply buttons
    aaxAnalyzeButton.onClick = [this]
    {
        if (processorRef.aaxModeParam != nullptr)
            processorRef.aaxModeParam->setValueNotifyingHost (0.0f);
        updateAAXModeButtons();
    };
    addAndMakeVisible (aaxAnalyzeButton);

    aaxApplyButton.onClick = [this]
    {
        if (processorRef.aaxModeParam != nullptr)
            processorRef.aaxModeParam->setValueNotifyingHost (1.0f);
        updateAAXModeButtons();
    };
    addAndMakeVisible (aaxApplyButton);

    // AAX Target Level slider
    aaxTargetLabel.setFont (juce::Font (12.0f));
    aaxTargetLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (aaxTargetLabel);

    aaxTargetSlider.setRange (-24.0, -12.0, 0.5);
    aaxTargetSlider.setValue ((processorRef.aaxTargetParam != nullptr)
                               ? (double) (float) (*processorRef.aaxTargetParam) : -18.0);
    aaxTargetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    aaxTargetSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    aaxTargetSlider.setColour (juce::Slider::thumbColourId,      colAccent);
    aaxTargetSlider.setColour (juce::Slider::trackColourId,      colAccent.withAlpha (0.4f));
    aaxTargetSlider.setColour (juce::Slider::backgroundColourId, colPanel);
    aaxTargetSlider.addListener (this);
    addAndMakeVisible (aaxTargetSlider);

    aaxTargetValLabel.setFont (juce::Font (12.0f));
    aaxTargetValLabel.setColour (juce::Label::textColourId, colText);
    aaxTargetValLabel.setJustificationType (juce::Justification::centredRight);
    aaxTargetValLabel.setText (juce::String (aaxTargetSlider.getValue(), 1) + " dBFS",
                               juce::dontSendNotification);
    addAndMakeVisible (aaxTargetValLabel);

    // AAX Max Gain slider
    aaxMaxGainLabel.setFont (juce::Font (12.0f));
    aaxMaxGainLabel.setColour (juce::Label::textColourId, colSubText);
    addAndMakeVisible (aaxMaxGainLabel);

    aaxMaxGainSlider.setRange (3.0, 24.0, 0.5);
    aaxMaxGainSlider.setValue ((processorRef.aaxMaxGainParam != nullptr)
                                ? (double) (float) (*processorRef.aaxMaxGainParam) : 12.0);
    aaxMaxGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    aaxMaxGainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    aaxMaxGainSlider.setColour (juce::Slider::thumbColourId,      colAccent);
    aaxMaxGainSlider.setColour (juce::Slider::trackColourId,      colAccent.withAlpha (0.4f));
    aaxMaxGainSlider.setColour (juce::Slider::backgroundColourId, colPanel);
    aaxMaxGainSlider.addListener (this);
    addAndMakeVisible (aaxMaxGainSlider);

    aaxMaxGainValLabel.setFont (juce::Font (12.0f));
    aaxMaxGainValLabel.setColour (juce::Label::textColourId, colText);
    aaxMaxGainValLabel.setJustificationType (juce::Justification::centredRight);
    aaxMaxGainValLabel.setText ("+/- " + juce::String (aaxMaxGainSlider.getValue(), 1) + " dB",
                                juce::dontSendNotification);
    addAndMakeVisible (aaxMaxGainValLabel);

    // Status label
    aaxStatusLabel.setFont (juce::Font (13.0f));
    aaxStatusLabel.setColour (juce::Label::textColourId, colSubText);
    aaxStatusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (aaxStatusLabel);

    if (isAAX)
    {
        // Hide the Standalone-only UI
        openButton      .setVisible (false);
        fileNameLabel   .setVisible (false);
        fileInfoLabel   .setVisible (false);
        mp3NoteLabel    .setVisible (false);
        waveformView    .setVisible (false);
        playPauseButton .setVisible (false);
        abControl       .setVisible (false);
        sectionDetectLabel.setVisible (false);
        wholeFileToggle   .setVisible (false);   // previously missed, so it showed in the AAX UI
        threshLabel   .setVisible (false);
        threshSlider  .setVisible (false);
        threshValLabel.setVisible (false);
        durLabel      .setVisible (false);
        durSlider     .setVisible (false);
        durValLabel   .setVisible (false);
        clipCountLabel.setVisible (false);
        sectionNormLabel.setVisible (false);
        targetLabel    .setVisible (false);
        targetSlider   .setVisible (false);
        targetValLabel .setVisible (false);
        maxGainLabel   .setVisible (false);
        maxGainSlider  .setVisible (false);
        maxGainValLabel.setVisible (false);
        peakLabel      .setVisible (false);
        peakSlider     .setVisible (false);
        peakValLabel   .setVisible (false);
        saveButton     .setVisible (false);
        saveStatusLabel.setVisible (false);
        clipListBox    .setVisible (false);

        setSize (420, 270);
        startTimerHz (10);  // refresh the AAX status readout
    }
    else
    {
        // Hide the AAX UI
        aaxTitleLabel   .setVisible (false);
        aaxModeLabel    .setVisible (false);
        aaxAnalyzeButton.setVisible (false);
        aaxApplyButton  .setVisible (false);
        aaxTargetLabel    .setVisible (false);
        aaxTargetSlider   .setVisible (false);
        aaxTargetValLabel .setVisible (false);
        aaxMaxGainLabel   .setVisible (false);
        aaxMaxGainSlider  .setVisible (false);
        aaxMaxGainValLabel.setVisible (false);
        aaxStatusLabel  .setVisible (false);
    }

    updateAAXModeButtons();

   #if JUCE_MAC
    // Standalone only: add Options (Audio/MIDI Settings) to the macOS menu bar
    if (! isAAX)
        juce::MenuBarModel::setMacMainMenu (this);
   #endif
}

VUClipGainNormalizerEditor::~VUClipGainNormalizerEditor()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    playPauseButton.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);   // detach before voxLAF is destroyed, to avoid a destruction-order accident
}

// ---- Switch to a native title bar once we are in a window (Standalone only) ----
void VUClipGainNormalizerEditor::parentHierarchyChanged()
{
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return;
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
    {
        if (! dw->isUsingNativeTitleBar())
        {
            dw->setUsingNativeTitleBar (true);
            dw->setName ("VOX Normalizer");
            dw->setContentComponentSize (620, 722);   // reapply, because going native resizes the window
        }
    }
}

// ---- Menu bar (Standalone: Options > Audio/MIDI Settings) ----
juce::StringArray VUClipGainNormalizerEditor::getMenuBarNames()
{
    return { "Options" };
}

juce::PopupMenu VUClipGainNormalizerEditor::getMenuForIndex (int, const juce::String&)
{
    juce::PopupMenu m;
    m.addItem (1, "Audio/MIDI Settings...");
    return m;
}

void VUClipGainNormalizerEditor::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == 1)
    {
       #if JucePlugin_Build_Standalone
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->showAudioSettingsDialog();
       #endif
    }
}

// ---- Painting ----

void VUClipGainNormalizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (colBg);

    // AAX only: title bar background plus an accent line, with aaxTitleLabel on top.
    // Standalone uses the native macOS title bar, so nothing is drawn here.
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        g.setColour (colPanel);
        g.fillRect (0, 0, getWidth(), 44);
        g.setColour (colAccent);
        g.drawLine (0.0f, 44.0f, (float) getWidth(), 44.0f, 1.5f);
    }

    // Drop zone (Standalone only, and only before a file is loaded)
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone
        && ! processorRef.getFileInfo().isValid())
    {
        auto dropZone = juce::Rectangle<int> (20, 78, getWidth() - 40, 132);  // same area as the waveform view
        g.setColour (isDraggingOver ? colDropHover : juce::Colour (0xff0d0d1a));
        g.fillRoundedRectangle (dropZone.toFloat(), 8.0f);
        g.setColour (isDraggingOver ? colAccent : colSubText);
        g.drawRoundedRectangle (dropZone.toFloat(), 8.0f, 1.5f);
        g.setFont (13.0f);
        g.drawText ("Drop a WAV / AIFF / MP3 file here",
                    dropZone, juce::Justification::centred);
    }

    // v1.2 Step 5-2: wrap Clip Detection / Normalize Settings in rounded cards
    // (like a SwiftUI GroupBox / Form. The look does not change once a file loads.)
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        vox::drawCard (g, clipCardBounds);
        vox::drawCard (g, normCardBounds);
    }
}

void VUClipGainNormalizerEditor::resized()
{
    const int margin = 20;
    const int w = getWidth() - margin * 2;

    // ---- AAX layout ----
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        // Title bar (drawn as a component here)
        aaxTitleLabel.setBounds (0, 0, getWidth(), 44);

        // Mode label and buttons
        aaxModeLabel    .setBounds (margin,              58,  60, 22);
        aaxAnalyzeButton.setBounds (margin + 68,         54, 110, 30);
        aaxApplyButton  .setBounds (margin + 68 + 118,   54, 110, 30);

        // Target Level
        aaxTargetLabel   .setBounds (margin + 8,        100, 140, 18);
        aaxTargetValLabel.setBounds (getWidth() - 110,  100,  90, 18);
        aaxTargetSlider  .setBounds (margin + 8,        118, w - 90, 24);

        // Max Gain
        aaxMaxGainLabel   .setBounds (margin + 8,       152, 160, 18);
        aaxMaxGainValLabel.setBounds (getWidth() - 110, 152,  90, 18);
        aaxMaxGainSlider  .setBounds (margin + 8,       170, w - 90, 24);

        // Status
        aaxStatusLabel.setBounds (margin, 206, w, 50);
        return;
    }

    // ---- Standalone layout from here down ----

    // ---- Open button (top right) and the file name on the same row ----
    // v1.1: with the native title bar, the old 38px header was removed and this moved up
    openButton   .setBounds (getWidth() - 140, 10, 120, 28);
    fileNameLabel.setBounds (margin, 12, getWidth() - margin - 150, 24);

    fileInfoLabel.setBounds (margin, 38, w, 18);
    mp3NoteLabel .setBounds (margin, 56, w, 16);

    // Waveform view (90px of waveform + a 42px label band)
    waveformView.setBounds (margin, 78, w, 90 + WaveformView::labelBandH);

    // ---- Preview controls (directly below the waveform, centred) ----
    // v1.2: a circular play button plus [ Original | Normalized ], centred on y = 232
    {
        const int rowCentreY = 232;
        const int playD      = 34;    // diameter of the circular button
        const int segW       = 200;
        const int segH       = 30;
        const int gap        = 12;

        const int totalW = playD + gap + segW;
        const int bx     = (getWidth() - totalW) / 2;

        playPauseButton.setBounds (bx, rowCentreY - playD / 2, playD, playD);
        abControl      .setBounds (bx + playD + gap, rowCentreY - segH / 2, segW, segH);
    }

    // ---- Shared card metrics ----
    // card          : x = margin .. getWidth() - margin
    // card contents : inset by pad (16) on both sides
    const int pad        = 16;
    const int contentX   = margin + pad;                 // 36
    const int contentW   = w - pad * 2;                  // 548
    const int valW       = 100;
    const int valX       = getWidth() - margin - pad - valW;  // 484, so the right edges line up
    const int labelH     = 18;
    const int sliderH    = 22;

    // ---- Card 1: Clip Detection (y = 260 .. 424, height 164) ----
    clipCardBounds = { margin, 260, w, 164 };

    sectionDetectLabel.setBounds (contentX, 272, 160, 22);
    wholeFileToggle   .setBounds (getWidth() - margin - pad - 120, 272, 120, 22);

    threshLabel   .setBounds (contentX, 300, 200, labelH);
    threshValLabel.setBounds (valX,     300, valW, labelH);
    threshSlider  .setBounds (contentX, 318, contentW, sliderH);

    durLabel   .setBounds (contentX, 348, 200, labelH);
    durValLabel.setBounds (valX,     348, valW, labelH);
    durSlider  .setBounds (contentX, 366, contentW, sliderH);

    clipCountLabel.setBounds (contentX, 392, contentW, 20);   // a little taller, to fit the 14pt bold font

    // ---- Card 2: Normalize Settings (y = 440 .. 626, height 186) ----
    normCardBounds = { margin, 440, w, 186 };

    sectionNormLabel.setBounds (contentX, 452, 220, 20);

    targetLabel   .setBounds (contentX, 478, 200, labelH);
    targetValLabel.setBounds (valX,     478, valW, labelH);
    targetSlider  .setBounds (contentX, 496, contentW, sliderH);

    maxGainLabel   .setBounds (contentX, 526, 200, labelH);
    maxGainValLabel.setBounds (valX,     526, valW, labelH);
    maxGainSlider  .setBounds (contentX, 544, contentW, sliderH);

    peakLabel   .setBounds (contentX, 574, 200, labelH);
    peakValLabel.setBounds (valX,     574, valW, labelH);
    peakSlider  .setBounds (contentX, 592, contentW, sliderH);

    // ---- Save button and status ----
    {
        const int btnW = 170;
        saveButton     .setBounds ((getWidth() - btnW) / 2, 646, btnW, 34);
        saveStatusLabel.setBounds (margin, 688, w, 18);
    }

    // The table is hidden; its job was taken over by the waveform overlay
    clipListBox.setBounds (0, 0, 0, 0);
}

// ---- Sliders ----

void VUClipGainNormalizerEditor::sliderValueChanged (juce::Slider* slider)
{
    if (slider == &threshSlider)
    {
        threshValLabel.setText (juce::String ((int) threshSlider.getValue()) + " dBFS",
                                juce::dontSendNotification);
        runDetection();
    }
    else if (slider == &durSlider)
    {
        durValLabel.setText (juce::String ((int) durSlider.getValue()) + " ms",
                             juce::dontSendNotification);
        runDetection();
    }
    else if (slider == &targetSlider)
    {
        targetValLabel.setText (juce::String (targetSlider.getValue(), 1) + " dBFS",
                                juce::dontSendNotification);
    }
    else if (slider == &maxGainSlider)
    {
        maxGainValLabel.setText ("+/- " + juce::String (maxGainSlider.getValue(), 1) + " dB",
                                 juce::dontSendNotification);
    }
    else if (slider == &peakSlider)
    {
        peakValLabel.setText (juce::String (peakSlider.getValue(), 1) + " dBFS",
                              juce::dontSendNotification);
    }
    else if (slider == &aaxTargetSlider)
    {
        float v = (float) aaxTargetSlider.getValue();
        aaxTargetValLabel.setText (juce::String (v, 1) + " dBFS",
                                   juce::dontSendNotification);
        if (processorRef.aaxTargetParam != nullptr)
            processorRef.aaxTargetParam->setValueNotifyingHost (
                processorRef.aaxTargetParam->getNormalisableRange().convertTo0to1 (v));
    }
    else if (slider == &aaxMaxGainSlider)
    {
        float v = (float) aaxMaxGainSlider.getValue();
        aaxMaxGainValLabel.setText ("+/- " + juce::String (v, 1) + " dB",
                                    juce::dontSendNotification);
        if (processorRef.aaxMaxGainParam != nullptr)
            processorRef.aaxMaxGainParam->setValueNotifyingHost (
                processorRef.aaxMaxGainParam->getNormalisableRange().convertTo0to1 (v));
    }
}

void VUClipGainNormalizerEditor::sliderDragEnded (juce::Slider* /*slider*/)
{
    // Re-analyse whenever any slider is released
    runAnalysis();
}

// ---- Greying out the silence-detection sliders while Whole file is on ----
void VUClipGainNormalizerEditor::setDetectionControlsEnabled (bool shouldBeEnabled)
{
    threshSlider.setEnabled (shouldBeEnabled);
    durSlider   .setEnabled (shouldBeEnabled);

    // A Label does not dim itself via setEnabled, so set the colour directly
    const juce::Colour dimSub = shouldBeEnabled ? colSubText : colSubText.withAlpha (0.40f);
    const juce::Colour dimVal = shouldBeEnabled ? colText    : colText   .withAlpha (0.40f);

    threshLabel   .setColour (juce::Label::textColourId, dimSub);
    durLabel      .setColour (juce::Label::textColourId, dimSub);
    threshValLabel.setColour (juce::Label::textColourId, dimVal);
    durValLabel   .setColour (juce::Label::textColourId, dimVal);

    threshLabel.repaint();  durLabel.repaint();
    threshValLabel.repaint(); durValLabel.repaint();
}

void VUClipGainNormalizerEditor::runDetection()
{
    if (! processorRef.getFileInfo().isValid()) return;

    VUClipGainNormalizerProcessor::DetectionParams params;
    params.thresholdDb  = (float) threshSlider.getValue();
    params.minSilenceMs = (int)   durSlider.getValue();
    params.wholeFile    = wholeFileToggle.getToggleState();
    processorRef.detectVirtualClips (params);
    updateDetectionDisplay();

    // Update the boundaries live, keeping the zoom and scroll position
    waveformView.setClips (&processorRef.getVirtualClips());
}

// ---- Timer (playback cursor updates) ----

void VUClipGainNormalizerEditor::timerCallback()
{
    // Refresh the AAX status
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        updateAAXStatusLabel();
        return;
    }

    // Update the playback cursor
    double len = processorRef.getPlaybackLengthSeconds();
    if (len > 0.0)
    {
        double pos = processorRef.getPlaybackPositionSeconds();
        waveformView.setPlaybackCursor (pos / len);

        // Has playback finished?
        if (pos >= len - 0.05 && ! processorRef.isPreviewPlaying())
        {
            processorRef.stopPreview();
            waveformView.setPlaybackCursor (-1.0);
            updatePlayButton();
            stopTimer();
        }
    }
}

void VUClipGainNormalizerEditor::updatePlayButton()
{
    bool playing = processorRef.isPreviewPlaying();
    symbolButtonLAF.showStop = playing;
    playPauseButton.repaint();  // repaint, because showStop on the LookAndFeel changed
}

bool VUClipGainNormalizerEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey
        && processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        if (! processorRef.getFileInfo().isValid()) return true;

        if (processorRef.isPreviewPlaying())
        {
            processorRef.pausePreview();
            updatePlayButton();
            stopTimer();
        }
        else
        {
            processorRef.startPreview (processorRef.isPreviewNormalized());
            updatePlayButton();
            startTimerHz (30);
        }
        return true;
    }
    return false;
}

void VUClipGainNormalizerEditor::updateABButtons()
{
    // Push the processor state into the segmented control (without notifying, to avoid a loop)
    abControl.setSelectedIndex (processorRef.isPreviewNormalized() ? 1 : 0, false);
}

// ---- Analyze ----

void VUClipGainNormalizerEditor::runAnalysis()
{
    if (! processorRef.getFileInfo().isValid()) return;

    VUClipGainNormalizerProcessor::AnalysisParams params;
    params.targetLevelDb = (float) targetSlider.getValue();
    params.maxGainDb     = (float) maxGainSlider.getValue();
    params.peakCeilingDb = (float) peakSlider.getValue();
    processorRef.analyzeClips (params);
    updateAnalysisDisplay();

    // Hand the analysis to the waveform view, which overlays it on the clips
    waveformView.setAnalyses (&processorRef.getClipAnalyses());

    // Rebuild the buffer holding the normalized audio for the waveform.
    // NOTE: this uses the same applyClipGains() as the export and the preview.
    // Reverting only this to a plain multiply causes what you see and hear to disagree.
    {
        const auto* raw = processorRef.getRawAudioData();

        if (raw != nullptr)
        {
            normalizedAudioBuf.makeCopyOf (*raw);
            VUClipGainNormalizerProcessor::applyClipGains (
                normalizedAudioBuf,
                processorRef.getVirtualClips(),
                processorRef.getClipAnalyses(),
                processorRef.getFileInfo().sampleRate);

            waveformView.setNormalizedData (&normalizedAudioBuf);
        }
    }
}

void VUClipGainNormalizerEditor::updateAnalysisDisplay()
{
    const auto& analyses = processorRef.getClipAnalyses();
    std::vector<ClipListModel::Row> rows;
    rows.reserve (analyses.size());

    for (const auto& a : analyses)
    {
        ClipListModel::Row row;
        row.index   = a.clipIndex;
        row.rmsDb   = a.rmsDb;
        row.gainDb  = a.gainDb;
        row.clamped = a.gainClamped;
        rows.push_back (row);
    }

    clipListModel.setRows (rows);
    clipListBox.updateContent();
}

// ---- Drag & drop ----

bool VUClipGainNormalizerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".mp3")
            return true;
    }
    return false;
}

void VUClipGainNormalizerEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    isDraggingOver = true;  repaint();
}
void VUClipGainNormalizerEditor::fileDragExit (const juce::StringArray&)
{
    isDraggingOver = false; repaint();
}
void VUClipGainNormalizerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    isDraggingOver = false;
    if (! files.isEmpty()) loadFileAndUpdate (juce::File (files[0]));
}

// ---- File dialogs ----

void VUClipGainNormalizerEditor::openSaveDialog()
{
    auto showError = [this] (const juce::String& msg)
    {
        saveStatusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffff6666));
        saveStatusLabel.setText (msg, juce::dontSendNotification);
    };

    if (! processorRef.getFileInfo().isValid())
    {
        showError ("Please load an audio file first.");
        return;
    }
    if (processorRef.getVirtualClips().empty())
    {
        showError ("No clips detected — try lowering the Silence Threshold.");
        return;
    }
    // Safety net: if the analysis is not current (wrong length, or empty), run it now.
    // There are paths where sliderDragEnded never fires, such as moving a slider with
    // the mouse wheel, so make sure everything agrees immediately before exporting.
    if (processorRef.getClipAnalyses().size() != processorRef.getVirtualClips().size())
        runAnalysis();

    if (processorRef.getClipAnalyses().empty())
    {
        showError ("Analysis not ready — move a slider to update.");
        return;
    }

    // Default file name: the original name plus _normalized
    auto& fi       = processorRef.getFileInfo();
    bool  isAiff   = (fi.format == "AIFF");
    juce::String defaultName = juce::File (fi.fileName).getFileNameWithoutExtension()
                               + "_normalized."
                               + (isAiff ? "aiff" : "wav");

    auto chooser = std::make_shared<juce::FileChooser> (
        "Save normalized file",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (defaultName),
        isAiff ? "*.aiff;*.aif" : "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode |
                          juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto result = fc.getResult();
                              if (! result.getFullPathName().isEmpty())
                              {
                                  saveStatusLabel.setColour (juce::Label::textColourId, colSubText);
                                  saveStatusLabel.setText ("Exporting...",
                                                           juce::dontSendNotification);
                                  bool ok = processorRef.applyAndExport (result);
                                  saveStatusLabel.setColour (
                                      juce::Label::textColourId,
                                      ok ? juce::Colour (0xff2ecc71) : juce::Colour (0xffff4455));
                                  saveStatusLabel.setText (
                                      ok ? ("Saved: " + result.getFileName())
                                         : "Export failed. Check file permissions.",
                                      juce::dontSendNotification);
                              }
                          });
}


void VUClipGainNormalizerEditor::openFileDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select an audio file",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode |
                          juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto result = fc.getResult();
                              if (result.existsAsFile())
                                  loadFileAndUpdate (result);
                          });
}

void VUClipGainNormalizerEditor::loadFileAndUpdate (const juce::File& file)
{
    if (processorRef.loadFile (file))
    {
        updateFileInfoDisplay();
        updateDetectionDisplay();
        waveformView.setAudioData (processorRef.getRawAudioData(),
                                   processorRef.getFileInfo().sampleRate,
                                   &processorRef.getVirtualClips());
        runAnalysis();

        // Show everything now that a file is loaded
        waveformView   .setVisible (true);
        playPauseButton.setVisible (true);
        abControl      .setVisible (true);

        repaint();
    }
    else
    {
        // ---- v1.2 Step 4: on a failed load, return the UI to the empty state ----
        // The processor has already run clearLoadedFile(), so match it exactly here.
        // Leaving the previous file on screen makes it impossible to tell why playback
        // and exporting no longer work.
        waveformView   .setVisible (false);
        playPauseButton.setVisible (false);
        abControl      .setVisible (false);
        mp3NoteLabel   .setVisible (false);

        waveformView.clearAll();     // discard the waveform, the analysis and the zoom state together
        normalizedAudioBuf.setSize (0, 0);

        fileNameLabel  .setText ({}, juce::dontSendNotification);
        fileInfoLabel  .setText ({}, juce::dontSendNotification);
        clipCountLabel .setText ({}, juce::dontSendNotification);
        saveStatusLabel.setText ({}, juce::dontSendNotification);

        updateAnalysisDisplay();
        repaint();   // redraw the drop zone

        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "Could not open file",
            "\"" + file.getFileName() + "\" could not be read.\n\n"
            "Supported formats: WAV / AIFF / MP3.\n"
            "The file may be corrupted, empty, or in an unsupported format.");
    }
}

void VUClipGainNormalizerEditor::updateFileInfoDisplay()
{
    auto& fi = processorRef.getFileInfo();
    if (! fi.isValid()) return;

    fileNameLabel.setText (fi.fileName, juce::dontSendNotification);

    double secs = fi.getLengthSeconds();
    int minutes  = (int) secs / 60;
    double rem   = secs - minutes * 60;
    juce::String timeStr = juce::String::formatted ("%d:%05.2f", minutes, rem);

    juce::String info;
    info << fi.format << "  |  "
         << (fi.numChannels == 1 ? "Mono" : "Stereo") << "  |  "
         << juce::String (fi.sampleRate / 1000.0, 1) << " kHz  |  "
         << fi.bitsPerSample << " bit  |  "
         << timeStr;

    fileInfoLabel.setText (info, juce::dontSendNotification);
    mp3NoteLabel.setVisible (processorRef.wasConvertedFromMp3());
}

// ---- AAX helpers ----

void VUClipGainNormalizerEditor::updateAAXModeButtons()
{
    const int mode = (processorRef.aaxModeParam != nullptr)
                     ? (int) (*processorRef.aaxModeParam) : 0;

    aaxAnalyzeButton.setColour (juce::TextButton::buttonColourId,
                                mode == 0 ? colAccent : colPanel);
    aaxAnalyzeButton.setColour (juce::TextButton::textColourOffId,
                                mode == 0 ? juce::Colours::white : colSubText);

    aaxApplyButton.setColour (juce::TextButton::buttonColourId,
                              mode == 1 ? colAccent : colPanel);
    aaxApplyButton.setColour (juce::TextButton::textColourOffId,
                              mode == 1 ? juce::Colours::white : colSubText);
}

void VUClipGainNormalizerEditor::updateAAXStatusLabel()
{
    const float rmsDb  = processorRef.aaxMeasuredRmsDb   .load();
    const float gainDb = processorRef.aaxCalculatedGainDb.load();

    if (rmsDb <= -119.0f)
    {
        aaxStatusLabel.setText ("Render in Analyze mode first.",
                                juce::dontSendNotification);
        aaxStatusLabel.setColour (juce::Label::textColourId, colSubText);
    }
    else
    {
        const juce::String gainStr = (gainDb >= 0.0f ? "+" : "")
                                     + juce::String (gainDb, 1) + " dB";
        aaxStatusLabel.setText ("RMS: " + juce::String (rmsDb, 1)
                                + " dBFS   →   Gain: " + gainStr,
                                juce::dontSendNotification);
        aaxStatusLabel.setColour (juce::Label::textColourId, colText);
    }
}

void VUClipGainNormalizerEditor::updateDetectionDisplay()
{
    auto& clips = processorRef.getVirtualClips();
    int count = (int) clips.size();

    if (count == 0)
        clipCountLabel.setText ("No virtual clips detected  —  try lowering Silence Threshold",
                                juce::dontSendNotification);
    else
        clipCountLabel.setText (juce::String (count) + " virtual clip" + (count > 1 ? "s" : "") + " detected",
                                juce::dontSendNotification);
}
