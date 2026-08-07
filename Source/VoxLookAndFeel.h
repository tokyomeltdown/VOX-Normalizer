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

// =====================================================================
//  VOX Normalizer — shared LookAndFeel and custom widgets for the modern UI
//  v1.2 / Step 5-1
//
//  Direction (matching LUFSBar / caps REC, i.e. the tone of macOS System Settings)
//    - Buttons  : 7px corner radius. Primary = coral fill / secondary = panel + hairline
//    - Sliders  : thin rounded 4px track + white circular knob + drop shadow
//    - Toggle   : checkbox replaced with a macOS-style switch
//    - Original/Normalized : two buttons replaced with a segmented control
//    - Colours are unchanged from v1.1 (dark only; no light mode)
// =====================================================================

namespace vox
{
    // ---- Colour palette (shared with PluginEditor.cpp) ----
    static const juce::Colour bg          { 0xff1c1c1e };  // background (near black)
    static const juce::Colour panel       { 0xff2c2c2e };  // cards / secondary buttons
    static const juce::Colour control     { 0xff48484a };  // track groove; bright enough to read on a card
    static const juce::Colour accent      { 0xffd97757 };  // warm coral
    static const juce::Colour text        { 0xfffcfcfc };  // pure white
    static const juce::Colour subText     { 0xff98989d };  // medium grey
    static const juce::Colour hairline    { 0x1affffff };  // border (white 10%)
    static const juce::Colour dropHover   { 0x33d97757 };

    static constexpr float cornerButton = 7.0f;
    static constexpr float cornerCard   = 10.0f;
    static constexpr int   knobRadius   = 9;    // slider knob radius

    // ---- Draws a section card. Used by the layout in Step 5-2 ----
    inline void drawCard (juce::Graphics& g, juce::Rectangle<int> area)
    {
        auto r = area.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, cornerCard);
        g.setColour (hairline);
        g.drawRoundedRectangle (r.reduced (0.5f), cornerCard, 1.0f);
    }

    // =================================================================
    //  VoxLookAndFeel : button + slider drawing
    //  Applied to the whole Editor so child components inherit it
    // =================================================================
    struct VoxLookAndFeel : public juce::LookAndFeel_V4
    {
        VoxLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId,          panel);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
            setColour (juce::PopupMenu::textColourId,                text);
        }

        // ---- Button background: rounded, lighter on hover, darker when pressed ----
        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool isHighlighted, bool isDown) override
        {
            auto b = button.getLocalBounds().toFloat().reduced (0.5f);

            auto fill = backgroundColour;
            if (! button.isEnabled())   fill = fill.withMultipliedAlpha (0.45f);
            else if (isDown)            fill = fill.darker   (0.22f);
            else if (isHighlighted)     fill = fill.brighter (0.12f);

            g.setColour (fill);
            g.fillRoundedRectangle (b, cornerButton);

            // Only dark (secondary) buttons get a hairline border so the surface reads
            if (fill.getPerceivedBrightness() < 0.35f)
            {
                g.setColour (hairline);
                g.drawRoundedRectangle (b, cornerButton, 1.0f);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
        {
            return juce::Font (13.0f, juce::Font::bold);
        }

        // ---- Knob radius. Also used to inset the ends of the track ----
        int getSliderThumbRadius (juce::Slider&) override { return knobRadius; }

        // ---- Horizontal slider: thin track + white circular knob ----
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float /*minPos*/, float /*maxPos*/,
                               juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height,
                                                        sliderPos, 0.0f, 0.0f, style, slider);
                return;
            }

            const bool  enabled = slider.isEnabled();
            const float trackH  = 4.0f;
            const float cy      = (float) y + (float) height * 0.5f;

            juce::Rectangle<float> track ((float) x, cy - trackH * 0.5f,
                                          (float) width, trackH);

            // Groove (the part the knob has not reached)
            g.setColour (enabled ? control : control.withAlpha (0.5f));
            g.fillRoundedRectangle (track, trackH * 0.5f);

            // Filled part (from the left up to the knob)
            auto filled = track.withRight (juce::jmax (track.getX() + trackH, sliderPos));
            g.setColour (enabled ? accent : accent.withAlpha (0.4f));
            g.fillRoundedRectangle (filled, trackH * 0.5f);

            // Knob (very slightly larger while hovered or dragged)
            const float r  = (float) knobRadius + (slider.isMouseOverOrDragging() ? 0.6f : 0.0f);
            const float kx = sliderPos - r;
            const float ky = cy - r;

            juce::Path knob;
            knob.addEllipse (kx, ky, r * 2.0f, r * 2.0f);

            juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 4, { 0, 1 })
                .drawForPath (g, knob);

            g.setColour (enabled ? juce::Colours::white : juce::Colours::white.withAlpha (0.5f));
            g.fillPath (knob);

            // Knob rim, so it does not dissolve into a light background
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawEllipse (kx, ky, r * 2.0f, r * 2.0f, 1.0f);
        }
    };

    // =================================================================
    //  SwitchButton : macOS-style toggle switch (drop-in for ToggleButton)
    //  getToggleState() / onClick work exactly as before
    // =================================================================
    class SwitchButton : public juce::ToggleButton
    {
    public:
        using juce::ToggleButton::ToggleButton;

        static constexpr int switchW = 34;
        static constexpr int switchH = 20;

        // When true the label sits to the left of the switch (macOS form-row order)
        void setTextOnLeft (bool shouldBeOnLeft) { textOnLeft = shouldBeOnLeft; repaint(); }

        void paintButton (juce::Graphics& g, bool isHighlighted, bool /*isDown*/) override
        {
            const float h  = (float) switchH;
            const float w  = (float) switchW;
            const float ty = ((float) getHeight() - h) * 0.5f;

            const float sx = textOnLeft ? (float) getWidth() - w : 0.0f;
            juce::Rectangle<float> sw (sx, ty, w, h);

            const bool on = getToggleState();
            auto trackCol = on ? accent : control;
            if (isHighlighted) trackCol = trackCol.brighter (0.12f);

            g.setColour (trackCol);
            g.fillRoundedRectangle (sw, h * 0.5f);

            // Knob
            const float pad = 2.0f;
            const float kd  = h - pad * 2.0f;
            const float kx  = on ? sw.getRight() - pad - kd : sw.getX() + pad;

            juce::Path knob;
            knob.addEllipse (kx, ty + pad, kd, kd);
            juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 3, { 0, 1 })
                .drawForPath (g, knob);
            g.setColour (juce::Colours::white);
            g.fillPath (knob);

            // Label
            g.setColour (findColour (juce::ToggleButton::textColourId));
            g.setFont (juce::Font (13.0f, juce::Font::bold));

            if (textOnLeft)
                g.drawText (getButtonText(), 0, 0, getWidth() - switchW - 8, getHeight(),
                            juce::Justification::centredRight, false);
            else
                g.drawText (getButtonText(), switchW + 8, 0, getWidth() - switchW - 8, getHeight(),
                            juce::Justification::centredLeft, false);
        }

    private:
        bool textOnLeft = false;
    };

    // =================================================================
    //  SegmentedControl : used for the Original / Normalized switch
    // =================================================================
    class SegmentedControl : public juce::Component
    {
    public:
        std::function<void (int)> onChange;   // fires on every click, even on the selected segment

        void addSegment (const juce::String& t) { segments.add (t); repaint(); }

        int  getSelectedIndex() const noexcept { return selected; }

        void setSelectedIndex (int i, bool notify)
        {
            selected = juce::jlimit (0, juce::jmax (0, segments.size() - 1), i);
            repaint();
            if (notify && onChange) onChange (selected);
        }

        void paint (juce::Graphics& g) override
        {
            if (segments.isEmpty()) return;

            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const float r = cornerButton;

            g.setColour (panel);
            g.fillRoundedRectangle (b, r);
            g.setColour (hairline);
            g.drawRoundedRectangle (b, r, 1.0f);

            const float segW = b.getWidth() / (float) segments.size();

            // Pill behind the selected segment
            {
                juce::Rectangle<float> pill (b.getX() + segW * (float) selected, b.getY(),
                                             segW, b.getHeight());
                pill = pill.reduced (2.0f);
                g.setColour (accent);
                g.fillRoundedRectangle (pill, r - 2.0f);
            }

            g.setFont (juce::Font (13.0f, juce::Font::bold));
            for (int i = 0; i < segments.size(); ++i)
            {
                juce::Rectangle<float> seg (b.getX() + segW * (float) i, b.getY(),
                                            segW, b.getHeight());
                juce::Colour c = (i == selected) ? juce::Colours::white
                                                 : (i == hovered ? text : subText);
                g.setColour (c);
                g.drawText (segments[i], seg.toNearestInt(), juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.x);
            if (i >= 0) setSelectedIndex (i, true);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.x);
            if (i != hovered) { hovered = i; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered != -1) { hovered = -1; repaint(); }
        }

    private:
        int indexAt (int x) const
        {
            if (segments.isEmpty() || getWidth() <= 0) return -1;
            return juce::jlimit (0, segments.size() - 1,
                                 x * segments.size() / getWidth());
        }

        juce::StringArray segments;
        int selected = 0;
        int hovered  = -1;
    };

    // =================================================================
    //  SymbolButtonLAF : Play/Stop button (circular coral, triangle/square drawn directly)
    //  Replaces the v1.1 class of the same name. showStop is used the same way
    // =================================================================
    struct SymbolButtonLAF : public juce::LookAndFeel_V4
    {
        bool showStop = false;   // false = play triangle, true = stop square

        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool isHighlighted, bool isDown) override
        {
            auto b = button.getLocalBounds().toFloat().reduced (0.5f);

            auto fill = backgroundColour;
            if (isDown)             fill = fill.darker   (0.22f);
            else if (isHighlighted) fill = fill.brighter (0.12f);

            g.setColour (fill);
            g.fillEllipse (b);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                             bool /*hover*/, bool /*down*/) override
        {
            g.setColour (btn.findColour (juce::TextButton::textColourOffId));

            const auto  b  = btn.getLocalBounds().toFloat();
            const float sz = juce::jmin (b.getWidth(), b.getHeight()) * 0.42f;
            const float cx = b.getCentreX();
            const float cy = b.getCentreY();

            if (showStop)
            {
                // Stop square (rounded corners to feel current)
                const float sq = sz * 0.80f;
                g.fillRoundedRectangle (cx - sq * 0.5f, cy - sq * 0.5f, sq, sq, 1.5f);
            }
            else
            {
                // Play triangle (nudged right so it looks optically centred)
                juce::Path tri;
                tri.addTriangle (cx - sz * 0.40f, cy - sz * 0.52f,
                                 cx - sz * 0.40f, cy + sz * 0.52f,
                                 cx + sz * 0.58f, cy);
                g.fillPath (tri);
            }
        }
    };

} // namespace vox
