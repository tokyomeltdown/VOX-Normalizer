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

#include "PluginProcessor.h"
#include "PluginEditor.h"

VUClipGainNormalizerProcessor::VUClipGainNormalizerProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();

    // ---- AAX AudioSuite parameter registration ----
    // addParameter takes ownership; the members below are non-owning pointers
    aaxModeParam = new juce::AudioParameterChoice (
        "aaxMode", "Mode",
        juce::StringArray { "Analyze", "Apply" }, 0);
    addParameter (aaxModeParam);

    aaxTargetParam = new juce::AudioParameterFloat (
        "aaxTarget", "Target Level",
        juce::NormalisableRange<float> (-24.0f, -12.0f, 0.5f), -18.0f);
    addParameter (aaxTargetParam);

    aaxMaxGainParam = new juce::AudioParameterFloat (
        "aaxMaxGain", "Max Gain",
        juce::NormalisableRange<float> (3.0f, 24.0f, 0.5f), 12.0f);
    addParameter (aaxMaxGainParam);

    // Persist a stereo device setup so the next launch starts correctly
    // applicationName = getName() so the path matches the one JUCE uses
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = getName();   // "VU Clip Gain Normalizer" (with spaces)
        opts.filenameSuffix      = ".settings";
        opts.osxLibrarySubFolder = "Application Support";

        juce::PropertiesFile props (opts);

        // Check the stored setup and fix it if ch0+ch1 are not enabled
        // Note: JUCE stores audioDeviceOutChans as a binary string ("11" = stereo)
        auto existingXml = props.getXmlValue ("audioSetup");

        juce::BigInteger currentOut;
        if (existingXml != nullptr)
            currentOut.parseString (existingXml->getStringAttribute ("audioDeviceOutChans", ""), 2);

        // Verify stereo output and that no input channels are enabled
        juce::BigInteger currentIn;
        if (existingXml != nullptr)
            currentIn.parseString (existingXml->getStringAttribute ("audioDeviceInChans", ""), 2);

        const bool needsFix = (! currentOut[0] || ! currentOut[1])  // not stereo out
                           || currentIn.getHighestBit() >= 0;        // an input channel is enabled

        if (needsFix)
        {
            juce::XmlElement setup ("DEVICESETUP");
            setup.setAttribute ("audioInputDeviceName",  "");   // no input device
            setup.setAttribute ("audioDeviceInChans",    "0");  // no input channels
            setup.setAttribute ("audioDeviceOutChans",   "11"); // binary "11" = ch0+ch1 = stereo
            props.setValue ("audioSetup", &setup);
        }

        // Suppress the feedback-loop warning banner.
        // JUCE Standalone defaults shouldMuteInput to true, so write false
        // explicitly to hide the warning from the next launch onwards.
        // (There is no input bus and no input channels, so there is no feedback risk.)
        if (! props.containsKey ("shouldMuteInput"))
            props.setValue ("shouldMuteInput", false);

        props.save();
    }
}

VUClipGainNormalizerProcessor::~VUClipGainNormalizerProcessor()
{
    // Detach the source explicitly first, otherwise the transportSource destructor
    // calls releaseResources() on an already-destroyed memAudioSource
    // and crashes.
    transportSource.stop();
    transportSource.setSource (nullptr);
}

void VUClipGainNormalizerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    transportSource.prepareToPlay (samplesPerBlock, sampleRate);

    // VU ballistics coefficients (derived from the block length)
    double blockDur = samplesPerBlock / sampleRate;
    attackSmooth = (float) std::exp (-blockDur / 0.010);  // 10ms attack
    decaySmooth  = (float) std::exp (-blockDur / 0.300);  // 300ms decay

    // Reset the AAX AudioSuite accumulator (reset per clip)
    analysisSumSq = 0.0;
    analysisCount = 0;
    // aaxMeasuredRmsDb / aaxCalculatedGainDb are kept between Analyze and Apply, so not reset
}

void VUClipGainNormalizerProcessor::releaseResources()
{
    transportSource.stop();
    transportSource.releaseResources();

    // End of an AAX Analyze pass: finalise the RMS and work out the gain.
    // NOTE: the compile-time macro (#if ! JucePlugin_Build_Standalone) is always
    // false in the Shared Code target, so test wrapperType at runtime instead.
    if (wrapperType != wrapperType_Standalone
        && aaxModeParam  != nullptr
        && (int) (*aaxModeParam) == 0   // Analyze
        && analysisCount > 0)
    {
        double rmsLinear = std::sqrt (analysisSumSq / (double) analysisCount);
        float  rmsDb     = (rmsLinear > 1.0e-9f)
                           ? juce::Decibels::gainToDecibels ((float) rmsLinear)
                           : -120.0f;

        float targetDb = (aaxTargetParam  != nullptr) ? (float) (*aaxTargetParam)  : -18.0f;
        float maxGDb   = (aaxMaxGainParam != nullptr) ? (float) (*aaxMaxGainParam) : 12.0f;

        float gainDb = targetDb - rmsDb;
        gainDb = juce::jlimit (-maxGDb, maxGDb, gainDb);

        aaxMeasuredRmsDb   .store (rmsDb);
        aaxCalculatedGainDb.store (gainDb);
    }
}

void VUClipGainNormalizerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer&)
{
    // ---- AAX AudioSuite ----
    // wrapperType is set at runtime by the AAX wrapper, which is more reliable than a macro
    if (wrapperType != wrapperType_Standalone)
    {
        const int mode    = (aaxModeParam != nullptr) ? (int) (*aaxModeParam) : 0;
        const int numSamp = buffer.getNumSamples();
        const int numCh   = buffer.getNumChannels();

        if (mode == 0)  // Analyze: accumulate RMS and pass the buffer through untouched
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* data = buffer.getReadPointer (ch);
                for (int i = 0; i < numSamp; ++i)
                    analysisSumSq += (double) data[i] * (double) data[i];
            }
            analysisCount += (int64_t) numSamp * numCh;

            // Update the running RMS and gain at the end of every block, so the values
            // are valid after an Analyze pass even if releaseResources() is never called
            if (analysisCount > 0)
            {
                double rmsLinear = std::sqrt (analysisSumSq / (double) analysisCount);
                float rmsDb = (rmsLinear > 1.0e-9f)
                              ? juce::Decibels::gainToDecibels ((float) rmsLinear)
                              : -120.0f;
                float targetDb = (aaxTargetParam  != nullptr) ? (float) (*aaxTargetParam)  : -18.0f;
                float maxGDb   = (aaxMaxGainParam != nullptr) ? (float) (*aaxMaxGainParam) : 12.0f;
                float gainDb   = juce::jlimit (-maxGDb, maxGDb, targetDb - rmsDb);
                aaxMeasuredRmsDb   .store (rmsDb);
                aaxCalculatedGainDb.store (gainDb);
            }
        }
        else  // Apply: apply the gain that was already calculated
        {
            const float gainLinear =
                juce::Decibels::decibelsToGain (aaxCalculatedGainDb.load());
            buffer.applyGain (gainLinear);
        }
        return;
    }

    // ---- Standalone preview playback ----
    if (transportSource.isPlaying())
    {
        // Pull audio from the transport
        juce::AudioSourceChannelInfo info (buffer);
        transportSource.getNextAudioBlock (info);

        // Only for mono sources: copy ch0 to every channel so it sits in the centre.
        // Stereo sources already have L+R in stereoPlayBuffer, set up by startPreview().
        if (previewSourceIsMono)
        {
            for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());
        }

        // RMS measurement (averaged across channels)
        float rms = 0.0f;
        int numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh; ++ch)
            rms += buffer.getRMSLevel (ch, 0, buffer.getNumSamples());
        rms /= (float) std::max (1, numCh);

        // Ballistics (fast attack, slow decay)
        float coeff = (rms > vuSmoothed) ? attackSmooth : decaySmooth;
        vuSmoothed  = rms + coeff * (vuSmoothed - rms);
        previewRmsLevel.store (vuSmoothed);
    }
    else
    {
        buffer.clear();
        // Keep decaying so the needle falls naturally
        vuSmoothed = decaySmooth * vuSmoothed;
        previewRmsLevel.store (vuSmoothed);
    }
}

// ---- File loading ----

// ---- v1.2 Step 4: throw away everything about the loaded file ----
//
// When loadFile failed, v1.1 only ran stopPreview / setSource(nullptr) before
// returning, so audioData, fileInfo and virtualClips still held the previous file.
// The UI kept showing that file while being unable to play it: a half-broken state.
void VUClipGainNormalizerProcessor::clearLoadedFile()
{
    stopPreview();
    transportSource.setSource (nullptr);
    memAudioSource.reset();

    audioData.reset();
    normalizedPreviewBuffer.setSize (0, 0);

    fileInfo = {};
    convertedFromMp3 = false;

    virtualClips.clear();
    clipAnalyses.clear();
    rmsEnvelopeDb.clear();
    envelopeHopSamples = 0;
}

bool VUClipGainNormalizerProcessor::loadFile (const juce::File& file)
{
    // Discard everything first, so a failure cannot leave a half-loaded state behind
    clearLoadedFile();

    if (! file.existsAsFile()) return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;

    // File info
    fileInfo.fileName      = file.getFileName();
    fileInfo.numChannels   = (int) reader->numChannels;
    fileInfo.sampleRate    = reader->sampleRate;
    fileInfo.bitsPerSample = (int) reader->bitsPerSample;
    fileInfo.numSamples    = (int64_t) reader->lengthInSamples;

    auto ext = file.getFileExtension().toLowerCase();
    if      (ext == ".wav")                    fileInfo.format = "WAV";
    else if (ext == ".aiff" || ext == ".aif")  fileInfo.format = "AIFF";
    else if (ext == ".mp3")                  { fileInfo.format = "MP3"; convertedFromMp3 = true; }
    else                                       fileInfo.format = ext.toUpperCase().trimCharactersAtStart(".");

    // Read the audio into memory
    int numCh = (int) reader->numChannels;
    int64_t numSamp = (int64_t) reader->lengthInSamples;

    // Reject files with zero length or zero channels (a corrupt header, for example).
    // Letting one through turns every later step into a divide by zero or an empty buffer.
    if (numCh <= 0 || numSamp <= 0)
    {
        clearLoadedFile();
        return false;
    }

    audioData = std::make_unique<juce::AudioBuffer<float>> (numCh, (int) numSamp);
    reader->read (audioData.get(), 0, (int) numSamp, 0, true, true);

    // v1.2 Step 3: build the RMS envelope once here; detection then runs on top of it
    buildRmsEnvelope();

    // Run detection immediately, with the current parameters
    detectVirtualClips (currentParams);

    return true;
}

// ---- v1.2 Step 3: build the RMS envelope (called once, from loadFile) ----
//
// Takes the RMS of non-overlapping 10ms blocks and stores it in dBFS.
// The RMS is computed the same way as in analyzeClips(): square per channel, then average.
void VUClipGainNormalizerProcessor::buildRmsEnvelope()
{
    rmsEnvelopeDb.clear();
    envelopeHopSamples = 0;

    if (audioData == nullptr || fileInfo.numSamples <= 0 || fileInfo.sampleRate <= 0.0)
        return;

    const int hop = juce::jmax (1, (int) (fileInfo.sampleRate * envelopeHopSeconds));
    envelopeHopSamples = hop;

    const int numCh   = audioData->getNumChannels();
    const int numSamp = audioData->getNumSamples();
    const int numHops = (numSamp + hop - 1) / hop;   // the trailing partial block still counts as one

    rmsEnvelopeDb.reserve ((size_t) numHops);

    for (int h = 0; h < numHops; ++h)
    {
        const int start = h * hop;
        const int len   = juce::jmin (hop, numSamp - start);
        if (len <= 0) break;

        double sumSq = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            // Reading through the channel pointer is far faster than a getSample loop
            const float* p = audioData->getReadPointer (ch, start);
            double chSumSq = 0.0;
            for (int s = 0; s < len; ++s)
                chSumSq += (double) p[s] * (double) p[s];
            sumSq += chSumSq;
        }

        const double meanSq = sumSq / ((double) len * (double) juce::jmax (1, numCh));
        const float  rmsLin = (float) std::sqrt (meanSq);

        rmsEnvelopeDb.push_back (rmsLin > 1.0e-9f
                                 ? juce::Decibels::gainToDecibels (rmsLin)
                                 : -120.0f);
    }
}

// ---- Silence detection ----

void VUClipGainNormalizerProcessor::detectVirtualClips (const DetectionParams& params)
{
    currentParams = params;
    virtualClips.clear();

    // *** If the clips change, any existing analysis is invalid. Clearing it here is what
    // stops a state where clips and analyses have different lengths, which would apply
    // a gain that was calculated for a different clip.
    // (Serious v1.1 bug: turning Whole file on left the analysis from the previous
    // detection in place, so the large boost calculated for the quiet intro clip was
    // applied to the entire song, sailing straight past Peak Ceiling and hard clipping at 0 dBFS.)
    clipAnalyses.clear();

    if (audioData == nullptr || fileInfo.numSamples == 0) return;

    // ---- Whole file mode: skip detection and use a single clip ----
    if (params.wholeFile)
    {
        VirtualClip clip;
        clip.startSample = 0;
        clip.numSamples  = fileInfo.numSamples;
        virtualClips.push_back (clip);
        return;
    }

    // ---- v1.2 Step 3: detect on top of the RMS envelope ----
    //
    // v1.1 tested the absolute value of a single sample against the threshold, so the
    // noise floor and breaths in a real recording made the decision jitter, and phrases
    // were split apart or run together.
    // Here the decision is made on a 10ms RMS, with hysteresis on top.
    if (rmsEnvelopeDb.empty() || envelopeHopSamples <= 0)
        buildRmsEnvelope();                       // safety net; normally loadFile has already built it
    if (rmsEnvelopeDb.empty() || envelopeHopSamples <= 0)
        return;

    const int     hop          = envelopeHopSamples;
    const int64_t totalSamples = fileInfo.numSamples;
    const int     numHops      = (int) rmsEnvelopeDb.size();

    // Threshold to enter a clip, and the lower one to leave it (hysteresis)
    const float openDb  = params.thresholdDb;
    const float closeDb = params.thresholdDb - detectionHysteresisDb;

    // Convert the minimum silence length into blocks (at least one)
    const int minSilenceHops = juce::jmax (
        1, (int) std::ceil (params.minSilenceMs / 1000.0 * fileInfo.sampleRate / hop));

    bool inClip        = false;
    int  clipStartHop  = 0;
    int  lastSoundHop  = 0;
    int  silenceHops   = 0;

    const int64_t minClipSamples = (int64_t) (fileInfo.sampleRate * minClipSeconds);

    auto pushClip = [&] (int startHop, int endHop)
    {
        VirtualClip clip;
        clip.startSample = (int64_t) startHop * hop;
        // The end is the end of the last block that had sound, clamped to the end of the file
        clip.numSamples  = juce::jmin ((int64_t) (endHop + 1) * hop, totalSamples)
                           - clip.startSample;

        // Anything too short is a spike in the noise floor, not a phrase, so drop it
        if (clip.numSamples >= minClipSamples)
            virtualClips.push_back (clip);
    };

    for (int h = 0; h < numHops; ++h)
    {
        const float level = rmsEnvelopeDb[(size_t) h];

        // Inside a clip use closeDb (the lenient threshold); outside use openDb (the strict one)
        const bool isSound = inClip ? (level >= closeDb) : (level >= openDb);

        if (isSound)
        {
            if (! inClip) { clipStartHop = h; inClip = true; }
            lastSoundHop = h;
            silenceHops  = 0;
        }
        else if (inClip)
        {
            if (++silenceHops >= minSilenceHops)
            {
                pushClip (clipStartHop, lastSoundHop);
                inClip      = false;
                silenceHops = 0;
            }
        }
    }

    // The file ended while still inside a clip
    if (inClip)
        pushClip (clipStartHop, lastSoundHop);
}

void VUClipGainNormalizerProcessor::moveClipBoundary (int clipIndex, bool isStart, int64_t newSamplePos)
{
    if (audioData == nullptr) return;
    if (clipIndex < 0 || clipIndex >= (int) virtualClips.size()) return;

    const int64_t totalSamples = (int64_t) audioData->getNumSamples();
    const int     n            = (int) virtualClips.size();
    const int64_t minSamples   = 1000;  // minimum clip length (about 23ms at 44.1kHz)

    if (isStart)
    {
        // Move the start of clip[clipIndex] (clipIndex should be > 0)
        auto& clip = virtualClips[(size_t) clipIndex];

        // Lower bound: the end of the previous clip. Upper bound: our own end - minSamples
        int64_t minPos = (clipIndex > 0)
            ? (virtualClips[(size_t)(clipIndex - 1)].startSample
               + virtualClips[(size_t)(clipIndex - 1)].numSamples)
            : 0;
        int64_t maxPos = clip.startSample + clip.numSamples - minSamples;

        int64_t clamped = juce::jlimit (minPos, juce::jmax (minPos, maxPos), newSamplePos);
        int64_t delta   = clamped - clip.startSample;
        clip.startSample += delta;
        clip.numSamples  -= delta;
    }
    else
    {
        // Move the end of clip[clipIndex] (clipIndex should be < n-1)
        auto& clip = virtualClips[(size_t) clipIndex];

        // Lower bound: our own start + minSamples. Upper bound: the next clip start, or the end of the file
        int64_t minPos = clip.startSample + minSamples;
        int64_t maxPos = (clipIndex < n - 1)
            ? virtualClips[(size_t)(clipIndex + 1)].startSample
            : totalSamples;

        int64_t clamped   = juce::jlimit (minPos, juce::jmax (minPos, maxPos), newSamplePos);
        clip.numSamples   = clamped - clip.startSample;
    }
}

// ---- Preview playback ----

// ---- Applying the clip gains, crossfades included (shared implementation) ----
//
// A gain ramp of boundaryFadeSeconds is placed at the head and tail of every clip.
//
// - No head ramp when the clip starts at the beginning of the file
// (there is nothing outside it to be discontinuous with; this is what stops
// whole-file mode from fading the first 10ms of a song in)
// - Likewise, no tail ramp when the clip ends at the end of the file
// - When two clips touch (which boundary dragging allows), a single head ramp on
// the later clip carries prevGain to thisGain. The earlier clip gets no tail ramp,
// because two ramps would return to 1.0 in between and dip the level.
// - Ramp length is capped at a third of the clip, so two ramps never overlap
void VUClipGainNormalizerProcessor::applyClipGains (juce::AudioBuffer<float>& buffer,
                                                    const std::vector<VirtualClip>&  clips,
                                                    const std::vector<ClipAnalysis>& analyses,
                                                    double sampleRate)
{
    const int numCh   = buffer.getNumChannels();
    const int numSamp = buffer.getNumSamples();
    if (numCh <= 0 || numSamp <= 0 || sampleRate <= 0.0) return;

    // *** Do nothing if the lengths disagree.
    // Applying them anyway would use a gain calculated for a different clip.
    if (clips.size() != analyses.size()) return;

    const size_t n = clips.size();
    const int rampMax = juce::jmax (1, (int) (sampleRate * boundaryFadeSeconds));

    for (size_t i = 0; i < n; ++i)
    {
        const auto& clip = clips[i];

        int start = (int) clip.startSample;
        int len   = (int) clip.numSamples;

        if (start < 0) { len += start; start = 0; }
        if (start >= numSamp || len <= 0) continue;
        if (start + len > numSamp) len = numSamp - start;
        if (len <= 0) continue;

        const float gain = juce::Decibels::decibelsToGain (analyses[i].gainDb);

        // Does this clip touch its neighbour?
        const bool touchesPrev = (i > 0)
            && (clips[i - 1].startSample + clips[i - 1].numSamples >= clip.startSample);
        const bool touchesNext = (i + 1 < n)
            && (clip.startSample + clip.numSamples >= clips[i + 1].startSample);

        const float prevGain = touchesPrev
            ? juce::Decibels::decibelsToGain (analyses[i - 1].gainDb)
            : 1.0f;

        const bool needStartRamp = (start > 0);
        const bool needEndRamp   = (! touchesNext) && (start + len < numSamp);

        const int ramp = juce::jmax (0, juce::jmin (rampMax, len / 3));

        const int flatStart = start + (needStartRamp ? ramp : 0);
        const int flatLen   = len   - (needStartRamp ? ramp : 0)
                                    - (needEndRamp   ? ramp : 0);

        for (int ch = 0; ch < numCh; ++ch)
        {
            if (needStartRamp && ramp > 0)
                buffer.applyGainRamp (ch, start, ramp, prevGain, gain);

            if (flatLen > 0)
                buffer.applyGain (ch, flatStart, flatLen, gain);

            if (needEndRamp && ramp > 0)
                buffer.applyGainRamp (ch, start + len - ramp, ramp, gain, 1.0f);
        }
    }
}

void VUClipGainNormalizerProcessor::buildNormalizedPreviewBuffer()
{
    if (audioData == nullptr) return;
    normalizedPreviewBuffer.makeCopyOf (*audioData);

    applyClipGains (normalizedPreviewBuffer, virtualClips, clipAnalyses, fileInfo.sampleRate);
}

void VUClipGainNormalizerProcessor::startPreview (bool useNormalized)
{
    if (audioData == nullptr || fileInfo.numSamples == 0) return;

    previewIsNormalized = useNormalized;

    // Read the position first: setSource(nullptr) resets the length to 0
    double pos = 0.0;
    if (transportSource.getLengthInSeconds() > 0.0)
        pos = transportSource.getCurrentPosition();

    // *** Detach from the audio thread FIRST (fixed in v1.2).
    //
    // v1.1 ran buildNormalizedPreviewBuffer() and stereoPlayBuffer.setSize()
    // before setSource(nullptr). If startPreview() was called during playback
    // (for example, clicking Normalized again on the segmented control while
    // Normalized was already playing), makeCopyOf / setSize would reallocate a
    // buffer that the audio thread was reading, which is a race on freed memory.
    // buffer that the audio thread was reading, which is a race on freed memory.
    //
    // AudioTransportSource::setSource(nullptr) takes the callback lock, so once it
    // returns the audio thread is no longer touching the buffers.
    transportSource.stop();
    transportSource.setSource (nullptr);
    memAudioSource.reset();

    // ---- Below here the buffers are detached from the audio thread and safe to rewrite ----

    if (useNormalized)
        buildNormalizedPreviewBuffer();

    juce::AudioBuffer<float>& srcBuf = useNormalized ? normalizedPreviewBuffer : *audioData;

    // Duplicate mono sources into a stereo buffer so they sit in the centre
    previewSourceIsMono = (srcBuf.getNumChannels() == 1);
    juce::AudioBuffer<float>* playBuf = &srcBuf;
    if (previewSourceIsMono)
    {
        stereoPlayBuffer.setSize (2, srcBuf.getNumSamples(), false, true, false);
        stereoPlayBuffer.copyFrom (0, 0, srcBuf, 0, 0, srcBuf.getNumSamples());
        stereoPlayBuffer.copyFrom (1, 0, srcBuf, 0, 0, srcBuf.getNumSamples());
        playBuf = &stereoPlayBuffer;
    }

    memAudioSource = std::make_unique<juce::MemoryAudioSource> (*playBuf, false, false);
    transportSource.setSource (memAudioSource.get(), 0, nullptr,
                                fileInfo.sampleRate,
                                playBuf->getNumChannels());
    transportSource.setPosition (pos);
    transportSource.start();
}

void VUClipGainNormalizerProcessor::pausePreview()
{
    transportSource.stop();   // the position is deliberately not reset
    vuSmoothed = 0.0f;
    previewRmsLevel.store (0.0f);
}

void VUClipGainNormalizerProcessor::stopPreview()
{
    transportSource.stop();
    transportSource.setPosition (0.0);
    vuSmoothed = 0.0f;
    previewRmsLevel.store (0.0f);
}

void VUClipGainNormalizerProcessor::togglePlayPause()
{
    if (transportSource.isPlaying())
        transportSource.stop();
    else
    {
        if (transportSource.getLengthInSeconds() <= 0.0)
            startPreview (previewIsNormalized);
        else
            transportSource.start();
    }
}

bool VUClipGainNormalizerProcessor::isPreviewPlaying() const
{
    return transportSource.isPlaying();
}

double VUClipGainNormalizerProcessor::getPlaybackPositionSeconds() const
{
    return transportSource.getCurrentPosition();
}

double VUClipGainNormalizerProcessor::getPlaybackLengthSeconds() const
{
    return transportSource.getLengthInSeconds();
}

void VUClipGainNormalizerProcessor::setPlaybackPositionSeconds (double s)
{
    transportSource.setPosition (s);
}

// ---- RMS measurement and gain calculation ----

void VUClipGainNormalizerProcessor::analyzeClips (const AnalysisParams& params)
{
    currentAnalysisParams = params;
    clipAnalyses.clear();

    if (audioData == nullptr || virtualClips.empty()) return;

    int numCh = audioData->getNumChannels();

    for (int i = 0; i < (int) virtualClips.size(); ++i)
    {
        const auto& clip = virtualClips[(size_t) i];

        // Mean square for the RMS and the largest absolute value for the peak, in one pass.
        //
        // v1.2 Step 3: calling getSample() per sample is slow, so use getReadPointer.
        // (Mathematically identical to v1.1: square per channel, then average = a correct stereo RMS.)
        const int bufSamples = audioData->getNumSamples();
        const int start = (int) juce::jlimit ((int64_t) 0, (int64_t) bufSamples, clip.startSample);
        const int count = (int) juce::jlimit ((int64_t) 0, (int64_t) (bufSamples - start),
                                              clip.numSamples);

        double sumSq   = 0.0;
        float  peakLin = 0.0f;

        // Avoids an out-of-range assert in getReadPointer(ch, start) when count is 0
        for (int ch = 0; ch < numCh && count > 0; ++ch)
        {
            const float* p = audioData->getReadPointer (ch, start);
            double chSumSq = 0.0;

            for (int s = 0; s < count; ++s)
            {
                const double v = (double) p[s];
                chSumSq += v * v;
                peakLin = juce::jmax (peakLin, std::abs (p[s]));  // the peak is the maximum across channels
            }
            sumSq += chSumSq;
        }

        float rmsLinear = (count > 0)
                          ? (float) std::sqrt (sumSq / ((double) count * (double) juce::jmax (1, numCh)))
                          : 0.0f;

        // Treat a very small RMS as a silent clip
        float rmsDb = (rmsLinear > 1.0e-9f)
                      ? juce::Decibels::gainToDecibels (rmsLinear)
                      : -120.0f;

        float peakDb = (peakLin > 1.0e-9f)
                       ? juce::Decibels::gainToDecibels (peakLin)
                       : -120.0f;

        // Gain needed to bring the RMS to Target
        float gainDb = params.targetLevelDb - rmsDb;
        ClipStatus status = ClipStatus::ok;

        // ---- 1. Clamp to Peak Ceiling ----
        // Work out how much gain the ceiling allows and clamp to it, so the peak after
        // the gain cannot exceed the limit. Without this, an RMS-driven boost can push
        // past 0 dBFS and distort when written as 16 or 24 bit.
        const float peakAllowedGainDb = params.peakCeilingDb - peakDb;
        if (gainDb > peakAllowedGainDb)
        {
            gainDb = peakAllowedGainDb;
            status = ClipStatus::peakLimited;
        }

        // ---- 2. Clamp to Max Gain ----
        bool clamped = false;
        if (gainDb > params.maxGainDb)
        {
            gainDb  = params.maxGainDb;
            clamped = true;
            status  = ClipStatus::maxGain;   // Max Gain bit before Peak Ceiling did
        }
        else if (gainDb < -params.maxGainDb)
        {
            gainDb  = -params.maxGainDb;
            clamped = true;
            // Distinguish "cannot come down far enough to meet the ceiling" from "simply cannot reach Target"
            status  = (peakDb + gainDb > params.peakCeilingDb + 0.01f)
                      ? ClipStatus::overCeiling
                      : ClipStatus::maxGain;
        }

        ClipAnalysis result;
        result.clipIndex   = i;
        result.rmsDb       = rmsDb;
        result.peakDb      = peakDb;
        result.gainDb      = gainDb;
        result.gainClamped = clamped;
        result.status      = status;
        clipAnalyses.push_back (result);
    }
}

// ---- Apply the gain and write the file ----

bool VUClipGainNormalizerProcessor::applyAndExport (const juce::File& outputFile)
{
    if (audioData == nullptr || fileInfo.numSamples == 0) return false;
    if (clipAnalyses.empty()) return false;

    // *** Safety valve: never export when the lengths disagree
    // (applying them would use a gain belonging to a different clip and distort)
    if (clipAnalyses.size() != virtualClips.size()) return false;

    const int numCh   = audioData->getNumChannels();
    const int numSamp = audioData->getNumSamples();

    // Copy the source into an output buffer
    juce::AudioBuffer<float> outBuffer (numCh, numSamp);
    for (int ch = 0; ch < numCh; ++ch)
        outBuffer.copyFrom (ch, 0, *audioData, ch, 0, numSamp);

    // Apply the clip gains, crossfades included, via the implementation shared by all three call sites
    applyClipGains (outBuffer, virtualClips, clipAnalyses, fileInfo.sampleRate);

    // Pick the output format (MP3 in becomes WAV out)
    juce::String ext = outputFile.getFileExtension().toLowerCase();
    std::unique_ptr<juce::AudioFormat> format;

    if (ext == ".aiff" || ext == ".aif")
        format = std::make_unique<juce::AiffAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    // ---- v1.2 Step 4: write to a temporary file, then swap it in ----
    //
    // v1.1 called outputFile.deleteFile() before creating the writer, so if the writer
    // failed to be created or the write failed, the original file was left deleted.
    // Overwriting the file you had loaded could therefore lose the original.
    // The real name is only taken once the temporary file has been written in full.
    juce::File tempFile = outputFile.getSiblingFile (
        outputFile.getFileNameWithoutExtension()
        + "_voxtmp_" + juce::String (juce::Random::getSystemRandom().nextInt (100000))
        + outputFile.getFileExtension());

    tempFile.deleteFile();

    bool wrote = false;
    {
        std::unique_ptr<juce::FileOutputStream> fos (tempFile.createOutputStream());
        if (fos == nullptr) return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            format->createWriterFor (fos.get(),
                                     fileInfo.sampleRate,
                                     (unsigned int) numCh,
                                     fileInfo.bitsPerSample,
                                     {},
                                     0));

        if (writer == nullptr) { tempFile.deleteFile(); return false; }

        fos.release();  // the writer now owns the stream
        wrote = writer->writeFromAudioSampleBuffer (outBuffer, 0, numSamp);
        // Destroy the writer here to close the file; it cannot be moved while open
    }

    if (! wrote)
    {
        tempFile.deleteFile();
        return false;
    }

    // Only now is it safe to remove the original
    if (outputFile.existsAsFile() && ! outputFile.deleteFile())
    {
        tempFile.deleteFile();
        return false;
    }

    if (! tempFile.moveFileTo (outputFile))
    {
        tempFile.deleteFile();
        return false;
    }

    return true;
}

juce::AudioProcessorEditor* VUClipGainNormalizerProcessor::createEditor()
{
    return new VUClipGainNormalizerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VUClipGainNormalizerProcessor();
}
