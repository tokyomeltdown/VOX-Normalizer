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

class VUClipGainNormalizerProcessor : public juce::AudioProcessor
{
public:
    VUClipGainNormalizerProcessor();
    ~VUClipGainNormalizerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }  // uses pluginName from the .jucer
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // ---- File info ----
    struct FileInfo
    {
        juce::String fileName;
        juce::String format;
        int    numChannels   = 0;
        double sampleRate    = 0.0;
        int    bitsPerSample = 0;
        int64_t numSamples   = 0;

        double getLengthSeconds() const
        {
            return (sampleRate > 0) ? (double) numSamples / sampleRate : 0.0;
        }
        bool isValid() const { return numSamples > 0; }
    };

    bool loadFile (const juce::File& file);

    // Throws away everything about the loaded file (also used when loadFile fails).
    // After calling this, getFileInfo().isValid() returns false
    void clearLoadedFile();
    const FileInfo& getFileInfo()      const { return fileInfo; }
    bool wasConvertedFromMp3()         const { return convertedFromMp3; }
    const juce::AudioBuffer<float>* getRawAudioData() const { return audioData.get(); }

    // ---- Silence detection parameters ----
    struct DetectionParams
    {
        float thresholdDb      = -50.0f;   // Silence Threshold (dBFS)
        int   minSilenceMs     = 100;      // Min. Silence Duration (ms)
        bool  wholeFile        = false;    // true = treat the whole file as a single clip
    };

    // ---- v1.2 Step 3: silence detection tuning ----
    // Envelope resolution. A finer hop gives more accurate boundaries but costs
    // memory and scan time. 10ms is ~18,000 entries for a 3 minute song, and the
    // rounding of boundaries is absorbed by the Step 2 crossfade.
    static constexpr double envelopeHopSeconds = 0.010;   // 10ms

    // Hysteresis. Entering a clip uses `threshold`; leaving it uses `threshold - this`.
    // Tolerating a lower level once inside a clip stops phrases from being split
    // apart by breaths or by the decay at the end of a line.
    static constexpr float detectionHysteresisDb = 3.0f;

    // Minimum clip length. Anything shorter is treated as a momentary spike in the
    // noise floor rather than a phrase, and is dropped (dropped = that region keeps
    // its original gain). In testing only blips at -44 to -49 dBFS were dropped and
    // no real phrase was lost. Keeping them would boost noise up to the Max Gain limit.
    static constexpr double minClipSeconds = 0.050;   // 50ms

    // ---- Virtual clip ----
    struct VirtualClip
    {
        int64_t startSample = 0;
        int64_t numSamples  = 0;

        double getStartSeconds  (double sr) const { return (sr > 0) ? startSample / sr : 0.0; }
        double getLengthSeconds (double sr) const { return (sr > 0) ? numSamples  / sr : 0.0; }
    };

    // Runs detection (call whenever a detection parameter changes)
    void detectVirtualClips (const DetectionParams& params);

    // Moves a boundary by hand (used by the waveform view's drag editing)
    // isStart = true  : moves the start of clip[clipIndex]
    // isStart = false : moves the end of clip[clipIndex]
    void moveClipBoundary (int clipIndex, bool isStart, int64_t newSamplePos);

    const std::vector<VirtualClip>& getVirtualClips() const { return virtualClips; }
    const DetectionParams&          getDetectionParams() const { return currentParams; }

    // ---- Analysis parameters ----
    struct AnalysisParams
    {
        float targetLevelDb  = -18.0f;  // Target Level (dBFS RMS)
        float maxGainDb      =  12.0f;  // Max Gain (dB, one side)
        float peakCeilingDb  =  -1.0f;  // Peak Ceiling (dBFS) - upper bound for the output peak
    };

    // ---- Analysis result for one clip ----
    // Records which constraint actually decided the gain
    enum class ClipStatus
    {
        ok,          // no constraint hit; matched Target
        maxGain,     // clamped by Max Gain (Target not reached)
        peakLimited, // clamped by Peak Ceiling (gain pulled back to avoid clipping)
        overCeiling  // Max Gain prevents satisfying Peak Ceiling (output exceeds the ceiling)
    };

    struct ClipAnalysis
    {
        int   clipIndex   = 0;
        float rmsDb       = 0.0f;   // measured RMS  (dBFS)
        float peakDb      = 0.0f;   // measured peak (dBFS)
        float gainDb      = 0.0f;   // gain applied  (dB)
        bool  gainClamped = false;  // clamped by Max Gain (kept for backwards compatibility)
        ClipStatus status = ClipStatus::ok;

        // Estimated RMS / peak after the gain is applied (dBFS)
        float resultRmsDb()  const { return rmsDb  + gainDb; }
        float resultPeakDb() const { return peakDb + gainDb; }
    };

    // Runs the analysis (on slider release and after a file is loaded)
    void analyzeClips (const AnalysisParams& params);

    // ---- v1.2 Step 2: crossfade at clip boundaries ----
    //
    // Applying gain as a hard step at a clip boundary produces a click, so a short
    // gain ramp is placed at the head and tail of every clip.
    //
    // *** Call this from all three places: export, preview and waveform display. ***
    //     Reverting any one of them to a plain applyGain produces a bug where what
    //     you see and what you hear disagree.
    static constexpr double boundaryFadeSeconds = 0.010;   // 10ms

    static void applyClipGains (juce::AudioBuffer<float>& buffer,
                                const std::vector<VirtualClip>&   clips,
                                const std::vector<ClipAnalysis>&  analyses,
                                double sampleRate);

    // Applies the gain and writes the file
    bool applyAndExport (const juce::File& outputFile);

    // ---- Preview playback ----
    void startPreview  (bool useNormalized);   // start from the current position
    void pausePreview  ();                      // pause, keeping the position
    void stopPreview   ();                      // stop and return to the start
    void togglePlayPause();                     // play / pause toggle
    bool isPreviewPlaying()       const;
    bool isPreviewNormalized()    const { return previewIsNormalized; }
    double getPlaybackPositionSeconds() const;
    double getPlaybackLengthSeconds()   const;
    void   setPlaybackPositionSeconds (double s);

    std::atomic<float> previewRmsLevel { 0.0f };  // read by the UI thread

    const std::vector<ClipAnalysis>& getClipAnalyses() const { return clipAnalyses; }
    const AnalysisParams&            getAnalysisParams() const { return currentAnalysisParams; }

    // ---- AAX AudioSuite parameters (registered with addParameter in the constructor) ----
    // Non-owning pointers; the AudioProcessor owns the parameters
    juce::AudioParameterChoice* aaxModeParam    = nullptr;  // 0=Analyze, 1=Apply
    juce::AudioParameterFloat*  aaxTargetParam  = nullptr;  // Target Level (dBFS)
    juce::AudioParameterFloat*  aaxMaxGainParam = nullptr;  // Max Gain (dB)

    // AAX analysis results (read by the UI thread)
    std::atomic<float> aaxMeasuredRmsDb    { -120.0f };  // measured RMS (dBFS)
    std::atomic<float> aaxCalculatedGainDb {    0.0f };  // gain applied (dB)

private:
    juce::AudioFormatManager formatManager;
    FileInfo fileInfo;
    bool convertedFromMp3 = false;

    // The loaded audio
    std::unique_ptr<juce::AudioBuffer<float>> audioData;

    std::vector<VirtualClip>  virtualClips;
    DetectionParams           currentParams;

    // ---- v1.2 Step 3: RMS envelope (the basis for silence detection) ----
    //
    // Built once in loadFile(). After that, moving a slider only walks this envelope
    // instead of rescanning the whole file.
    // Even a 3 minute song is only ~18,000 entries, so the scan is instant.
    std::vector<float> rmsEnvelopeDb;      // one entry = RMS of envelopeHopSamples (dBFS)
    int                envelopeHopSamples = 0;

    void buildRmsEnvelope();

    std::vector<ClipAnalysis> clipAnalyses;
    AnalysisParams            currentAnalysisParams;

    // Preview
    juce::AudioTransportSource               transportSource;
    std::unique_ptr<juce::MemoryAudioSource> memAudioSource;
    juce::AudioBuffer<float>                 normalizedPreviewBuffer;
    bool  previewIsNormalized = false;
    bool  previewSourceIsMono = false;  // tells processBlock whether to copy mono to stereo
    float vuSmoothed          = 0.0f;
    float attackSmooth        = 0.90f;   // recalculated in prepareToPlay
    float decaySmooth         = 0.97f;

    void buildNormalizedPreviewBuffer();

    juce::AudioBuffer<float> stereoPlayBuffer;   // used to convert mono to stereo

    // AAX AudioSuite analysis accumulator (filled in processBlock, finalised in releaseResources)
    double  analysisSumSq = 0.0;
    int64_t analysisCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUClipGainNormalizerProcessor)
};
