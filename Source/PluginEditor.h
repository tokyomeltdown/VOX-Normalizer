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

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "VoxLookAndFeel.h"

// ---- Table model for the clip list ----
class ClipListModel : public juce::TableListBoxModel
{
public:
    struct Row
    {
        int   index     = 0;
        float rmsDb     = 0.0f;
        float gainDb    = 0.0f;
        bool  clamped   = false;
    };

    void setRows (const std::vector<Row>& r) { rows = r; }
    int getNumRows() override { return (int) rows.size(); }

    void paintRowBackground (juce::Graphics& g, int /*row*/, int w, int h, bool selected) override
    {
        g.fillAll (selected ? juce::Colour (0xff223355) : juce::Colour (0xff0d0d1a));
    }

    void paintCell (juce::Graphics& g, int row, int col,
                    int w, int h, bool /*selected*/) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        const auto& r = rows[(size_t) row];
        g.setColour (r.clamped ? juce::Colour (0xffefb854) : juce::Colour (0xffffffff));
        g.setFont (12.0f);

        juce::String text;
        switch (col)
        {
            case 0: text = juce::String (r.index + 1);                                break;
            case 1: text = juce::String (r.rmsDb,  1) + " dBFS";                     break;
            case 2: text = (r.gainDb >= 0 ? "+" : "") + juce::String (r.gainDb, 1) + " dB"; break;
            case 3: text = r.clamped ? "WARN" : "OK"; break;
            default: break;
        }
        g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft);
    }

private:
    std::vector<Row> rows;
};

// ※ v1.2 Step 5-1:
//    The old WholeFileLAF (checkbox) and SymbolButtonLAF (square button) were
//    replaced by vox::SwitchButton / vox::SymbolButtonLAF in VoxLookAndFeel.h.

class VUClipGainNormalizerEditor : public juce::AudioProcessorEditor,
                                   public juce::FileDragAndDropTarget,
                                   public juce::Slider::Listener,
                                   public juce::MenuBarModel,
                                   public juce::Timer
{
public:
    explicit VUClipGainNormalizerEditor (VUClipGainNormalizerProcessor&);
    ~VUClipGainNormalizerEditor() override;

    void paint      (juce::Graphics&) override;
    void resized    () override;
    bool keyPressed (const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;

    // MenuBarModel (the Options menu in the Standalone build)
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // Drag & drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;

    // Slider changes
    void sliderValueChanged (juce::Slider* slider) override;
    void sliderDragEnded    (juce::Slider* slider) override;

    // Timer (30fps: playback cursor updates)
    void timerCallback() override;

private:
    void openFileDialog();
    void openSaveDialog();
    void updatePlayButton();
    void loadFileAndUpdate (const juce::File& file);
    void updateFileInfoDisplay();
    void updateDetectionDisplay();
    void runDetection();
    void runAnalysis();
    void updateAnalysisDisplay();

    // Greys out the silence-detection sliders while Whole file is on (does not hide them)
    void setDetectionControlsEnabled (bool shouldBeEnabled);

    // Computed in resized(), used by paint() to draw the section cards
    juce::Rectangle<int> clipCardBounds;
    juce::Rectangle<int> normCardBounds;

    VUClipGainNormalizerProcessor& processorRef;

    // ---- UI widgets ----
    juce::TextButton openButton { "Open File..." };

    // File info
    juce::Label fileNameLabel;
    juce::Label fileInfoLabel;
    juce::Label mp3NoteLabel;

    // Clip Detection section
    juce::Label        sectionDetectLabel { {}, "Clip Detection" };
    vox::SwitchButton  wholeFileToggle    { "Whole file" };

    juce::Label  threshLabel  { {}, "Silence Threshold" };
    juce::Slider threshSlider;
    juce::Label  threshValLabel;

    juce::Label  durLabel  { {}, "Min. Silence Duration" };
    juce::Slider durSlider;
    juce::Label  durValLabel;

    // Detection result
    juce::Label clipCountLabel;

    // ---- Normalize Settings section ----
    juce::Label sectionNormLabel { {}, "Normalize Settings" };

    juce::Label  targetLabel    { {}, "Target Level" };
    juce::Slider targetSlider;
    juce::Label  targetValLabel;

    juce::Label  maxGainLabel   { {}, "Max Gain (+/- dB)" };
    juce::Slider maxGainSlider;
    juce::Label  maxGainValLabel;

    juce::Label  peakLabel   { {}, "Peak Ceiling" };
    juce::Slider peakSlider;
    juce::Label  peakValLabel;


    // ---- Clip list ----
    ClipListModel     clipListModel;
    juce::TableListBox clipListBox;

    WaveformView  waveformView;

    // ---- Buffer holding the normalized audio for the waveform display ----
    juce::AudioBuffer<float> normalizedAudioBuf;

    // ---- Preview controls ----
    vox::VoxLookAndFeel   voxLAF;            // applied to the whole Editor; children inherit it
    vox::SymbolButtonLAF  symbolButtonLAF;   // applied to the Play button only
    juce::TextButton      playPauseButton;   // play / stop toggle (circular)
    vox::SegmentedControl abControl;         // [ Original | Normalized ]

    void updateABButtons();  // pushes the processor state into the segmented control

    // ---- Save ----
    juce::TextButton saveButton { "Save As..." };
    juce::Label      saveStatusLabel;

    // ---- AAX AudioSuite UI (only shown when wrapperType is AAX) ----
    juce::Label       aaxTitleLabel      { {}, "VOX Normalizer  |  AAX AudioSuite" };
    juce::Label       aaxModeLabel       { {}, "Mode" };
    juce::TextButton  aaxAnalyzeButton   { "Analyze" };
    juce::TextButton  aaxApplyButton     { "Apply" };
    juce::Label       aaxTargetLabel     { {}, "Target Level" };
    juce::Slider      aaxTargetSlider;
    juce::Label       aaxTargetValLabel;
    juce::Label       aaxMaxGainLabel    { {}, "Max Gain (+/- dB)" };
    juce::Slider      aaxMaxGainSlider;
    juce::Label       aaxMaxGainValLabel;
    juce::Label       aaxStatusLabel;    // shows RMS / gain

    void updateAAXModeButtons();   // updates the Analyze/Apply highlight
    void updateAAXStatusLabel();   // refreshes the analysis readout

    bool isDraggingOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUClipGainNormalizerEditor)
};
