#!/bin/bash
# ============================================================
# Builds the command-line test harness (tests/build/harness).
#
# Run  bash build.sh  first: the harness links against the Shared Code library
# it leaves in build/Debug, and uses the JuceLibraryCode it regenerates.
# Apple Silicon only (the Debug build is native arm64).
# ============================================================
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_MODULES=~/Documents/JUCE/modules
LIB="$ROOT/build/Debug/libVOX Normalizer.a"
OUT="$ROOT/tests/build/harness"

if [ ! -f "$LIB" ]; then
    echo "ERROR: $LIB not found. Run bash build.sh first."
    exit 1
fi

MODULES="juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_plugin_client
         juce_audio_processors juce_audio_processors_headless juce_audio_utils juce_core
         juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra"
DEFINES="-DDEBUG=1 -D_DEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
         -DJUCE_STRICT_REFCOUNTEDPOINTER=1 -DJUCE_VST3_CAN_REPLACE_VST2=0"
for m in $MODULES; do
    DEFINES="$DEFINES -DJUCE_MODULE_AVAILABLE_$m=1"
done

FRAMEWORKS=""
for f in Accelerate AudioToolbox Cocoa CoreAudio CoreAudioKit CoreMIDI DiscRecording \
         Foundation IOKit Metal MetalKit QuartzCore Security WebKit; do
    FRAMEWORKS="$FRAMEWORKS -framework $f"
done

mkdir -p "$(dirname "$OUT")"
clang++ -std=c++17 -arch arm64 -mmacosx-version-min=11.0 -g -O1 $DEFINES \
    -include "$ROOT/JuceLibraryCode/JucePluginDefines.h" \
    -I"$ROOT/JuceLibraryCode" -I"$JUCE_MODULES" -I"$ROOT/Source" \
    "$ROOT/tests/harness.cpp" "$LIB" $FRAMEWORKS -o "$OUT"

echo "built $OUT"
