/*
    This file is part of VOX Normalizer.
    Copyright (C) 2026 tokyomeltdown

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

// ---- Waveform + clip boundaries + per-clip overlay + zoom ----
//
// Zoom behaviour:
// - zoomFactor: 1x to 16x (1x shows the whole file)
// - scrollOffset: 0.0 to (1 - 1/zoom), in file-normalised coordinates
// - Vertical wheel: zoom in/out around the mouse position
// - Horizontal wheel: pan
// - [-][+] buttons, top right: halve / double the zoom around the centre
// - Drag: pans, but only while zoomed in
// - Click (without dragging): moves the playback position
// ---------------------------------------------------------------

class WaveformView : public juce::Component
{
public:
    static constexpr int labelBandH = 42;  // height of the label band along the bottom

    WaveformView() = default;

    // Sets the audio and the clip list
    void setAudioData (const juce::AudioBuffer<float>* data,
                       double sampleRate,
                       const std::vector<VUClipGainNormalizerProcessor::VirtualClip>* clips)
    {
        audioData    = data;
        sr           = sampleRate;
        virtualClips = clips;
        // Loading a new file resets the zoom
        zoomFactor   = 1.0f;
        scrollOffset = 0.0f;
        buildWaveformCache();
        repaint();
    }

    // Updates only the clip boundaries, keeping the zoom and scroll position
    // Used when a slider changes: the audio is the same, only the boundaries moved
    void setClips (const std::vector<VUClipGainNormalizerProcessor::VirtualClip>* clips)
    {
        virtualClips = clips;
        repaint();
    }

    // Sets the analysis results (call after analyzeClips)
    void setAnalyses (const std::vector<VUClipGainNormalizerProcessor::ClipAnalysis>* analyses)
    {
        clipAnalyses = analyses;
        repaint();
    }

    // Called when the waveform is clicked, to move the playback position (set by the Editor)
    std::function<void(double)> onCursorClick;

    // Called when a boundary drag finishes
    // clipIndex: index of the clip that moved
    // isStart: true = start, false = end
    // newSamplePos: the new sample position
    std::function<void(int clipIndex, bool isStart, int64_t newSamplePos)> onBoundaryDragged;

    // Playback cursor, 0.0 to 1.0. A negative value hides it
    void setPlaybackCursor (double normalizedPos)
    {
        playbackCursor = normalizedPos;
        repaint();
    }

    // Sets the normalized audio, used when the display is switched
    void setNormalizedData (const juce::AudioBuffer<float>* data)
    {
        normalizedData = data;
        if (showingNormalized)
        {
            buildWaveformCache();
            repaint();
        }
    }

    // Switches the display between Original and Normalized
    void showNormalized (bool useNorm)
    {
        if (showingNormalized == useNorm) return;
        showingNormalized = useNorm;
        buildWaveformCache();
        repaint();
    }

    void clearAll()
    {
        audioData        = nullptr;
        normalizedData   = nullptr;
        showingNormalized = false;
        sr             = 1.0;
        virtualClips   = nullptr;
        clipAnalyses   = nullptr;
        playbackCursor = -1.0;
        zoomFactor     = 1.0f;
        scrollOffset   = 0.0f;
        waveformCache.clear();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int w     = getWidth();
        const int h     = getHeight();
        const int waveH = h - labelBandH;

        if (audioData == nullptr || waveformCache.empty())
            return;

        // ---- Background ----
        g.setColour (juce::Colour (0xff0d0d1a));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        // ---- Label band background ----
        g.setColour (juce::Colour (0xff0a0a14));
        g.fillRect (0, waveH, w, labelBandH);
        g.setColour (juce::Colour (0xff378add).withAlpha (0.25f));
        g.drawLine (0.0f, (float) waveH, (float) w, (float) waveH, 0.5f);

        // ---- Waveform (waveformCache matches the current visible range) ----
        const float cx = (float) waveH * 0.5f;
        const float h2 = (float) waveH * 0.43f;

        // Fill
        g.setColour (juce::Colour (0xff378add).withAlpha (0.35f));
        juce::Path fillPath;
        fillPath.startNewSubPath (0.0f, cx);
        for (int x = 0; x < w; ++x)
        {
            float peak = (x < (int) waveformCache.size()) ? waveformCache[(size_t) x] : 0.0f;
            fillPath.lineTo ((float) x, cx - peak * h2);
        }
        for (int x = w - 1; x >= 0; --x)
        {
            float peak = (x < (int) waveformCache.size()) ? waveformCache[(size_t) x] : 0.0f;
            fillPath.lineTo ((float) x, cx + peak * h2);
        }
        fillPath.closeSubPath();
        g.fillPath (fillPath);

        // Outline along the top
        g.setColour (juce::Colour (0xff378add).withAlpha (0.85f));
        juce::Path outline;
        bool first = true;
        for (int x = 0; x < w; ++x)
        {
            float peak = (x < (int) waveformCache.size()) ? waveformCache[(size_t) x] : 0.0f;
            float y    = cx - peak * h2;
            if (first) { outline.startNewSubPath ((float) x, y); first = false; }
            else         outline.lineTo ((float) x, y);
        }
        g.strokePath (outline, juce::PathStrokeType (1.0f));

        // ---- Clip boundaries + labels ----
        if (virtualClips != nullptr && !virtualClips->empty())
        {
            const int64_t totalSamples = (int64_t) audioData->getNumSamples();
            if (totalSamples > 0)
            {
                for (size_t i = 0; i < virtualClips->size(); ++i)
                {
                    const auto& clip = (*virtualClips)[i];

                    float normStart = (float) clip.startSample / (float) totalSamples;
                    float normEnd   = (float) (clip.startSample + clip.numSamples) / (float) totalSamples;

                    // Convert to zoomed coordinates
                    float xStart = normalizedToScreenX (normStart);
                    float xEnd   = normalizedToScreenX (normEnd);
                    float clipW  = xEnd - xStart;

                    // Skip clips that are off screen
                    if (xEnd < 0.0f || xStart > (float) w) continue;

                    float drawXStart = juce::jmax (0.0f, xStart);
                    float drawXEnd   = juce::jmin ((float) w, xEnd);
                    float drawW      = drawXEnd - drawXStart;

                    // ---- Clip region highlight ----
                    g.setColour (juce::Colour (0xff378add).withAlpha (0.06f));
                    g.fillRect (drawXStart, 0.0f, drawW, (float) waveH);

                    // ---- Boundary lines (red; yellow while being dragged) ----
                    // Is this the boundary currently being dragged?
                    auto isBoundaryDragged = [&](int clipIdx, bool isStartBound) -> bool
                    {
                        if (dragBoundaryClipIndex < 0) return false;
                        if (dragBoundaryIsStart)
                        {
                            if (isStartBound  && clipIdx == dragBoundaryClipIndex)     return true;
                            if (!isStartBound && clipIdx == dragBoundaryClipIndex - 1) return true;
                        }
                        else
                        {
                            if (!isStartBound && clipIdx == dragBoundaryClipIndex)     return true;
                            if (isStartBound  && clipIdx == dragBoundaryClipIndex + 1) return true;
                        }
                        return false;
                    };

                    bool startDragged = isBoundaryDragged ((int) i, true);
                    bool endDragged   = isBoundaryDragged ((int) i, false);

                    // Start line
                    {
                        float lineX = startDragged ? dragBoundaryScreenX : xStart;
                        if (lineX >= 0.0f && lineX <= (float) w)
                        {
                            g.setColour (startDragged ? juce::Colour (0xffffffaa)
                                                      : juce::Colour (0xffff4455).withAlpha (0.85f));
                            g.drawLine (lineX, 0.0f, lineX, (float) waveH, startDragged ? 2.0f : 1.2f);
                        }
                    }

                    // End line
                    {
                        float lineX = endDragged ? dragBoundaryScreenX : xEnd;
                        if (lineX >= 0.0f && lineX <= (float) w)
                        {
                            g.setColour (endDragged ? juce::Colour (0xffffffaa)
                                                    : juce::Colour (0xffff4455).withAlpha (0.85f));
                            g.drawLine (lineX, 0.0f, lineX, (float) waveH, endDragged ? 2.0f : 1.2f);
                        }
                    }

                    // ---- Label band text ----
                    using ClipStatus = VUClipGainNormalizerProcessor::ClipStatus;
                    ClipStatus status = ClipStatus::ok;
                    float rmsDb      = 0.0f;
                    float gainDb     = 0.0f;

                    if (clipAnalyses != nullptr && i < clipAnalyses->size())
                    {
                        const auto& a = (*clipAnalyses)[i];
                        rmsDb  = a.rmsDb;
                        gainDb = a.gainDb;
                        status = a.status;
                    }

                    // Clip number, small, at the left edge
                    g.setColour (juce::Colour (0xff378add).withAlpha (0.7f));
                    g.setFont (juce::Font (10.0f, juce::Font::bold));
                    g.drawText ("#" + juce::String ((int) i + 1),
                                (int) drawXStart + 3, waveH + 2, 24, 14,
                                juce::Justification::centredLeft, false);

                    if (clipAnalyses != nullptr && i < clipAnalyses->size())
                    {
                        // RMS
                        g.setColour (juce::Colour (0xff888780));
                        g.setFont (juce::Font (10.5f));
                        g.drawText (juce::String (rmsDb, 1) + " dB",
                                    (int) drawXStart, waveH + 2, (int) drawW, 14,
                                    juce::Justification::centred, false);

                        // Gain + Status
                        // OK   ... matched Target
                        // MAX  ... clamped by Max Gain (Target not reached)
                        // PEAK ... clamped by Peak Ceiling (gain pulled back to avoid clipping)
                        // CLIP ... Max Gain prevents satisfying Peak Ceiling (needs attention)
                        juce::String statusStr;
                        juce::Colour statusCol;
                        switch (status)
                        {
                            case ClipStatus::maxGain:
                                statusStr = "MAX";  statusCol = juce::Colour (0xffefb854); break;
                            case ClipStatus::peakLimited:
                                statusStr = "PEAK"; statusCol = juce::Colour (0xff67b0ff); break;
                            case ClipStatus::overCeiling:
                                statusStr = "CLIP"; statusCol = juce::Colour (0xffff5b52); break;
                            case ClipStatus::ok:
                            default:
                                statusStr = "OK";   statusCol = juce::Colour (0xff6ae06a); break;
                        }

                        g.setColour (statusCol);
                        g.setFont (juce::Font (11.0f, juce::Font::bold));
                        juce::String gainStr = (gainDb >= 0 ? "+" : "") + juce::String (gainDb, 1)
                                               + " dB  " + statusStr;
                        g.drawText (gainStr, (int) drawXStart, waveH + 18, (int) drawW, 16,
                                    juce::Justification::centred, false);
                    }
                }
            }
        }

        // ---- Playback cursor ----
        if (playbackCursor >= 0.0 && playbackCursor <= 1.0)
        {
            float xCursor = normalizedToScreenX ((float) playbackCursor);
            if (xCursor >= 0.0f && xCursor <= (float) w)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawLine (xCursor, 0.0f, xCursor, (float) waveH, 1.5f);

                // Triangular marker at the top of the cursor
                juce::Path tri;
                tri.addTriangle (xCursor - 5.0f, 0.0f,
                                 xCursor + 5.0f, 0.0f,
                                 xCursor,        8.0f);
                g.fillPath (tri);
            }
        }

        // ---- Zoom buttons ----
        drawZoomButtons (g);

        // ---- Zoom level readout (hidden at 1x) ----
        if (zoomFactor > 1.01f)
        {
            juce::String zoomStr = juce::String (zoomFactor, 1) + "x";
            // Drop the decimal when the factor is a whole number (rare with 1.25x steps, but tidy)
            if (std::abs (zoomFactor - std::round (zoomFactor)) < 0.05f)
                zoomStr = juce::String ((int) std::round (zoomFactor)) + "x";

            g.setColour (juce::Colour (0xff378add).withAlpha (0.55f));
            g.setFont (juce::Font (10.0f));
            // Placed just left of the buttons
            g.drawText (zoomStr, w - 90, 4, 48, 16,
                        juce::Justification::centredRight, false);
        }

        // ---- Outer border (only once a file is loaded) ----
        g.setColour (juce::Colour (0xff378add).withAlpha (0.35f));
        g.drawRoundedRectangle (getLocalBounds().toFloat(), 4.0f, 0.5f);
    }

private:
    const juce::AudioBuffer<float>*                                 audioData      = nullptr;
    const juce::AudioBuffer<float>*                                 normalizedData = nullptr;
    bool                                                            showingNormalized = false;
    const std::vector<VUClipGainNormalizerProcessor::VirtualClip>*  virtualClips = nullptr;
    const std::vector<VUClipGainNormalizerProcessor::ClipAnalysis>* clipAnalyses = nullptr;
    double sr             = 1.0;
    double playbackCursor = -1.0;  // 0.0 to 1.0; negative hides it

    // ---- Zoom state ----
    float zoomFactor   = 1.0f;  // 1x to 16x
    float scrollOffset = 0.0f;  // file-normalised coordinate, 0.0 to (1 - 1/zoom)

    // ---- Drag state (pan / cursor) ----
    bool  isDragging      = false;
    bool  buttonClicked   = false;
    float dragStartScroll = 0.0f;

    // ---- Boundary drag state ----
    int   dragBoundaryClipIndex = -1;    // -1 = not dragging
    bool  dragBoundaryIsStart   = false; // true = start, false = end
    float dragBoundaryScreenX   = 0.0f;

    std::vector<float> waveformCache;

    // ---- Zoom / scroll helpers ----

    float viewEnd() const { return scrollOffset + 1.0f / zoomFactor; }

    void clampScroll()
    {
        float maxScroll = juce::jmax (0.0f, 1.0f - 1.0f / zoomFactor);
        scrollOffset = juce::jlimit (0.0f, maxScroll, scrollOffset);
    }

    // file-normalised position -> screen X in pixels
    float normalizedToScreenX (float normPos) const
    {
        float range = 1.0f / zoomFactor;
        return (normPos - scrollOffset) / range * (float) getWidth();
    }

    // screen X in pixels -> file-normalised position
    float screenXToNormalized (float x) const
    {
        float range = 1.0f / zoomFactor;
        return scrollOffset + x / (float) getWidth() * range;
    }

    // ---- Zoom button areas ----
    juce::Rectangle<float> minusButtonBounds() const
    {
        return { (float) getWidth() - 38.0f, 4.0f, 16.0f, 16.0f };
    }
    juce::Rectangle<float> plusButtonBounds() const
    {
        return { (float) getWidth() - 20.0f, 4.0f, 16.0f, 16.0f };
    }

    // Boundary hit test (returns an inner boundary within 8px)
    struct BoundaryHit { int clipIndex; bool isStart; };

    std::optional<BoundaryHit> hitTestBoundary (float screenX) const
    {
        if (virtualClips == nullptr || audioData == nullptr || virtualClips->empty()) return {};
        const int64_t totalSamples = (int64_t) audioData->getNumSamples();
        if (totalSamples <= 0) return {};

        const int     n    = (int) virtualClips->size();
        constexpr float hitR = 8.0f;

        for (int i = 0; i < n; ++i)
        {
            const auto& clip = (*virtualClips)[(size_t) i];

            // Start (the start of clip 0 is the head of the file, which is fixed, so it is excluded)
            if (i > 0)
            {
                float x = normalizedToScreenX ((float) clip.startSample / (float) totalSamples);
                if (std::abs (screenX - x) <= hitR) return BoundaryHit{ i, true };
            }

            // End (the end of the last clip is the end of the file, which is fixed, so it is excluded)
            if (i < n - 1)
            {
                float x = normalizedToScreenX (
                    (float)(clip.startSample + clip.numSamples) / (float) totalSamples);
                if (std::abs (screenX - x) <= hitR) return BoundaryHit{ i, false };
            }
        }
        return {};
    }

    void drawZoomButtons (juce::Graphics& g)
    {
        auto drawBtn = [&](juce::Rectangle<float> b, const juce::String& label, bool enabled)
        {
            g.setColour (enabled
                         ? juce::Colour (0xff378add).withAlpha (0.40f)
                         : juce::Colour (0xff378add).withAlpha (0.15f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (enabled
                         ? juce::Colour (0xffffffff).withAlpha (0.85f)
                         : juce::Colour (0xffffffff).withAlpha (0.30f));
            g.setFont (juce::Font (12.0f, juce::Font::bold));
            g.drawText (label, b.toNearestInt(), juce::Justification::centred, false);
        };

        drawBtn (minusButtonBounds(), "-", zoomFactor > 1.01f);
        drawBtn (plusButtonBounds(),  "+", zoomFactor < 15.9f);
    }

    // ---- Cache build (covers the currently visible zoom/scroll range) ----
    void buildWaveformCache()
    {
        waveformCache.clear();
        if (audioData == nullptr || getWidth() <= 0) return;

        // Show either the original or the normalized data
        const juce::AudioBuffer<float>* activeData =
            (showingNormalized && normalizedData != nullptr) ? normalizedData : audioData;

        const int w       = getWidth();
        const int numCh   = activeData->getNumChannels();
        const int numSamp = activeData->getNumSamples();
        if (numSamp == 0 || numCh == 0) return;

        waveformCache.resize ((size_t) w, 0.0f);

        // Fetch the channel pointers up front (much faster than getSample)
        std::vector<const float*> chPtrs ((size_t) numCh);
        for (int ch = 0; ch < numCh; ++ch)
            chPtrs[(size_t) ch] = activeData->getReadPointer (ch);

        // visible range (file-normalised) -> sample range
        const float vs = scrollOffset;
        const float ve = viewEnd();

        for (int x = 0; x < w; ++x)
        {
            float normStart = vs + (float)  x      / (float) w * (ve - vs);
            float normEnd   = vs + (float) (x + 1) / (float) w * (ve - vs);

            int sStart = juce::jlimit (0, numSamp, (int) (normStart * (float) numSamp));
            int sEnd   = juce::jlimit (0, numSamp, (int) (normEnd   * (float) numSamp));
            if (sEnd <= sStart) sEnd = sStart + 1;
            if (sEnd > numSamp) sEnd = numSamp;

            float peak = 0.0f;
            const float invCh = 1.0f / (float) numCh;
            for (int s = sStart; s < sEnd; ++s)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                    sum += std::abs (chPtrs[(size_t) ch][s]);
                peak = std::max (peak, sum * invCh);
            }
            waveformCache[(size_t) x] = peak;
        }
    }

    // ---- Mouse events ----

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (audioData == nullptr) return;

        buttonClicked = false;
        isDragging    = false;

        // The label band is not clickable or draggable
        if (e.y >= getHeight() - labelBandH) return;

        // Zoom buttons
        if (minusButtonBounds().contains (e.position))
        {
            buttonClicked = true;
            if (zoomFactor > 1.01f)
            {
                // Zoom out, keeping the centre of the view fixed
                float center = scrollOffset + 0.5f / zoomFactor;
                zoomFactor   = juce::jmax (1.0f, zoomFactor / 2.0f);
                scrollOffset = center - 0.5f / zoomFactor;
                clampScroll();
                buildWaveformCache();
                repaint();
            }
            return;
        }
        if (plusButtonBounds().contains (e.position))
        {
            buttonClicked = true;
            if (zoomFactor < 15.9f)
            {
                float center = scrollOffset + 0.5f / zoomFactor;
                zoomFactor   = juce::jmin (16.0f, zoomFactor * 2.0f);
                scrollOffset = center - 0.5f / zoomFactor;
                clampScroll();
                buildWaveformCache();
                repaint();
            }
            return;
        }

        // Boundary hit test, which takes priority over panning and the cursor
        auto hit = hitTestBoundary ((float) e.x);
        if (hit.has_value())
        {
            dragBoundaryClipIndex = hit->clipIndex;
            dragBoundaryIsStart   = hit->isStart;
            dragBoundaryScreenX   = (float) e.x;
            return;  // do not fall through to pan / cursor handling
        }

        // Remember where the drag started (for panning)
        dragStartScroll = scrollOffset;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (audioData == nullptr) return;

        // While dragging a boundary, follow the mouse and repaint
        if (dragBoundaryClipIndex >= 0)
        {
            dragBoundaryScreenX = (float) e.x;
            repaint();
            return;
        }

        // No panning is needed when not zoomed in
        if (buttonClicked || zoomFactor <= 1.01f) return;

        isDragging = true;
        // Dragging right moves the content right, i.e. the window shifts left, so scrollOffset decreases
        float dx      = (float) e.getDistanceFromDragStartX();
        float dScroll = -dx / (float) getWidth() * (1.0f / zoomFactor);
        scrollOffset  = dragStartScroll + dScroll;
        clampScroll();
        buildWaveformCache();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // Boundary drag finished: work out the sample position and fire the callback
        if (dragBoundaryClipIndex >= 0)
        {
            if (onBoundaryDragged != nullptr && audioData != nullptr)
            {
                float normPos    = juce::jlimit (0.0f, 1.0f,
                                   screenXToNormalized (dragBoundaryScreenX));
                int64_t newSamplePos = (int64_t) (normPos * (float) audioData->getNumSamples());
                onBoundaryDragged (dragBoundaryClipIndex, dragBoundaryIsStart, newSamplePos);
            }
            dragBoundaryClipIndex = -1;
            repaint();
            return;
        }

        if (audioData == nullptr || buttonClicked) return;
        if (e.y >= getHeight() - labelBandH) { isDragging = false; return; }

        // Only treat it as a cursor move if the drag was small (<= 4px)
        bool wasClick = (std::abs (e.getDistanceFromDragStartX()) <= 4 &&
                         std::abs (e.getDistanceFromDragStartY()) <= 4);

        if (wasClick && !isDragging)
        {
            double normPos = juce::jlimit (0.0, 1.0,
                             (double) screenXToNormalized ((float) e.x));
            if (onCursorClick) onCursorClick (normPos);
        }
        isDragging = false;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (audioData == nullptr) return;
        auto hit = hitTestBoundary ((float) e.x);
        setMouseCursor (hit.has_value()
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (audioData == nullptr) return;
        if (e.y >= getHeight() - labelBandH) return;

        const float absX = std::abs (wheel.deltaX);
        const float absY = std::abs (wheel.deltaY);

        if (absY >= absX && absY > 0.001f)
        {
            // Vertical wheel: zoom around the mouse position
            float normMousePos = screenXToNormalized ((float) e.x);

            float factor = (wheel.deltaY > 0.0f) ? 1.25f : (1.0f / 1.25f);
            zoomFactor   = juce::jlimit (1.0f, 16.0f, zoomFactor * factor);

            // Adjust the scroll so the normalised position under the mouse stays put
            scrollOffset = normMousePos - (float) e.x / (float) getWidth() * (1.0f / zoomFactor);
            clampScroll();
        }
        else if (absX > 0.001f)
        {
            // Horizontal wheel: pan
            float panDelta = -wheel.deltaX * (1.0f / zoomFactor);
            scrollOffset  += panDelta;
            clampScroll();
        }

        buildWaveformCache();
        repaint();
    }

    void resized() override { buildWaveformCache(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
