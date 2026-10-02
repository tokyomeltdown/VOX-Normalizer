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

// Command-line test harness.
//
// Links against the Shared Code library that build.sh produces, and drives the
// real VUClipGainNormalizerProcessor in the same order as the Editor:
// loadFile -> detectVirtualClips -> (boundary drags) -> analyzeClips -> applyAndExport.
// run_tests.py feeds it synthetic files and checks the exported audio.
//
// usage: harness <in> <out> [--thresh dB] [--dur ms] [--whole]
//                [--target dB] [--maxgain dB] [--ceiling dB]
//                [--move <clipIndex> start|end <samplePos>]...

#include <JuceHeader.h>
#include "PluginProcessor.h"

static const char* statusName (VUClipGainNormalizerProcessor::ClipStatus s)
{
    using S = VUClipGainNormalizerProcessor::ClipStatus;
    switch (s)
    {
        case S::ok:          return "OK";
        case S::maxGain:     return "MAX";
        case S::peakLimited: return "PEAK";
        case S::overCeiling: return "CLIP";
    }
    return "?";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::printf ("usage: harness <in> <out> [options]\n"); return 2; }

    // Absolute paths, so juce::File does not assert
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File in  = cwd.getChildFile (juce::String (argv[1]));
    const juce::File out = cwd.getChildFile (juce::String (argv[2]));

    VUClipGainNormalizerProcessor::DetectionParams dp;
    VUClipGainNormalizerProcessor::AnalysisParams  ap;
    struct Move { int idx; bool isStart; int64_t pos; };
    std::vector<Move> moves;

    for (int i = 3; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if      (a == "--thresh"  && i + 1 < argc) dp.thresholdDb   = (float) std::atof (argv[++i]);
        else if (a == "--dur"     && i + 1 < argc) dp.minSilenceMs  = std::atoi (argv[++i]);
        else if (a == "--whole")                   dp.wholeFile     = true;
        else if (a == "--target"  && i + 1 < argc) ap.targetLevelDb = (float) std::atof (argv[++i]);
        else if (a == "--maxgain" && i + 1 < argc) ap.maxGainDb     = (float) std::atof (argv[++i]);
        else if (a == "--ceiling" && i + 1 < argc) ap.peakCeilingDb = (float) std::atof (argv[++i]);
        else if (a == "--move"    && i + 3 < argc)
        {
            Move m;
            m.idx     = std::atoi (argv[++i]);
            m.isStart = juce::String (argv[++i]) == "start";
            m.pos     = (int64_t) std::atoll (argv[++i]);
            moves.push_back (m);
        }
        else { std::printf ("unknown option: %s\n", argv[i]); return 2; }
    }

    VUClipGainNormalizerProcessor p;
    if (! p.loadFile (in)) { std::printf ("LOAD FAILED\n"); return 1; }

    const auto& fi = p.getFileInfo();
    std::printf ("loaded: %s ch=%d sr=%.0f bits=%d len=%lld mp3=%d\n",
                 fi.format.toRawUTF8(), fi.numChannels, fi.sampleRate, fi.bitsPerSample,
                 (long long) fi.numSamples, (int) p.wasConvertedFromMp3());

    p.detectVirtualClips (dp);
    for (const auto& m : moves)
        p.moveClipBoundary (m.idx, m.isStart, m.pos);
    p.analyzeClips (ap);

    const auto& clips = p.getVirtualClips();
    const auto& an    = p.getClipAnalyses();
    std::printf ("clips=%d\n", (int) clips.size());
    for (size_t i = 0; i < clips.size() && i < an.size(); ++i)
        std::printf ("  #%zu start=%lld len=%lld rms=%.2f peak=%.2f gain=%+.2f %s\n", i + 1,
                     (long long) clips[i].startSample, (long long) clips[i].numSamples,
                     an[i].rmsDb, an[i].peakDb, an[i].gainDb, statusName (an[i].status));

    const bool ok = p.applyAndExport (out);
    std::printf ("export=%s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
