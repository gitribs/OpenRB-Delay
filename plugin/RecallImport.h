#pragma once

#include <JuceHeader.h>

struct DelayRecall
{
    int mix = 0;
    int steps = 1;
    int mode = 0;
    int feedback = 0;
    int pan = 0;
};

// empty = success; otherwise a short reason
juce::String parseRecall (const juce::String& text, DelayRecall& out);
void applyRecall (juce::AudioProcessorValueTreeState& apvts, const DelayRecall& values);
