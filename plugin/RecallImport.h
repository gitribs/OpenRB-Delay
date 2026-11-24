#pragma once

#include <JuceHeader.h>

#include <vector>

struct DelayRecall
{
    int mix = 0;
    int steps = 1;
    int mode = 0;
    int feedback = 0;
    int pan = 0;
    int enabled = 1;
};

struct DelayPrintEvent
{
    int tick = 0;
    const char* id = "";
    int value = 0;
};

struct DelayPrint
{
    int ppq = 192;
    std::vector<DelayPrintEvent> events;
};

// empty = success; otherwise a short reason
juce::String parseRecall (const juce::String& text, DelayRecall& out);
void applyRecall (juce::AudioProcessorValueTreeState& apvts, const DelayRecall& values);

// empty = success. Missing or non-v1 print is success with no events.
juce::String parsePrint (const juce::String& text, DelayPrint& out);
