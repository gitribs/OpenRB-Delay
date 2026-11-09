#include "PluginProcessor.h"
#include "RecallImport.h"

#include <iostream>

namespace
{
int gFails = 0;

void expectEqual (int a, int b, const char* msg)
{
    if (a != b)
    {
        std::cerr << "FAIL: " << msg << " (" << a << " != " << b << ")\n";
        ++gFails;
    }
}

void expectString (const juce::String& a, const juce::String& b, const char* msg)
{
    if (a != b)
    {
        std::cerr << "FAIL: " << msg << " (\"" << a << "\" != \"" << b << "\")\n";
        ++gFails;
    }
}

int paramInt (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* v = apvts.getRawParameterValue (id))
        return juce::roundToInt (v->load());
    return -999;
}

const juce::String kModloader =
    "openrb-delay 1\n"
    "mix: 22\n"
    "steps: 8\n"
    "mode: sixteenth\n"
    "feedback: 70\n"
    "pan: 64\n";

const juce::String kTriplet =
    "openrb-delay 1\n"
    "mix: 10\n"
    "steps: 3\n"
    "mode: triplet\n"
    "feedback: 40\n"
    "pan: 0\n";
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    {
        DelayRecall rec;
        expectString (parseRecall (kModloader, rec), {}, "parse modloader");
        expectEqual (rec.mix, 22, "modloader mix");
        expectEqual (rec.steps, 8, "modloader steps");
        expectEqual (rec.mode, 1, "modloader mode sixteenth");
        expectEqual (rec.feedback, 70, "modloader feedback");
        expectEqual (rec.pan, 64, "modloader pan");
    }

    {
        DelayRecall rec;
        expectString (parseRecall (kTriplet, rec), {}, "parse triplet");
        expectEqual (rec.mix, 10, "triplet mix");
        expectEqual (rec.mode, 0, "triplet mode");
    }

    {
        DelayRecall rec;
        expectString (parseRecall ("openrb-distort 1\namount: 1\nshape: 2\n", rec),
                      "not OpenRB Delay data",
                      "wrong header");
    }

    {
        DelayRecall rec;
        expectString (parseRecall ("openrb-delay 2\nmix: 1\n", rec),
                      "unsupported recall version",
                      "bad version");
    }

    {
        DelayRecall rec;
        const juce::String missing =
            "openrb-delay 1\n"
            "mix: 22\n"
            "steps: 8\n"
            "mode: sixteenth\n"
            "pan: 64\n";
        expectString (parseRecall (missing, rec), "missing feedback", "missing key");
    }

    {
        DelayRecall rec;
        const juce::String banana =
            "openrb-delay 1\n"
            "mix: 22\n"
            "steps: 8\n"
            "mode: banana\n"
            "feedback: 70\n"
            "pan: 64\n";
        expectString (parseRecall (banana, rec), "invalid mode", "mode banana");
    }

    {
        DelayRecall rec;
        const juce::String clamped =
            "openrb-delay 1\n"
            "mix: 200\n"
            "steps: 0\n"
            "mode: sixteenth\n"
            "feedback: 70\n"
            "pan: -3\n"
            "unknown: 1\n";
        expectString (parseRecall (clamped, rec), {}, "clamp parse");
        expectEqual (rec.mix, 127, "clamp mix");
        expectEqual (rec.steps, 1, "clamp steps");
        expectEqual (rec.pan, 0, "clamp pan");
    }

    {
        DelayProcessor proc;
        expectString (proc.importRecall (kModloader), {}, "import modloader");
        expectEqual (paramInt (proc.apvts, "mix"), 22, "apply mix");
        expectEqual (paramInt (proc.apvts, "steps"), 8, "apply steps");
        expectEqual (paramInt (proc.apvts, "mode"), 1, "apply mode sixteenth");
        expectEqual (paramInt (proc.apvts, "feedback"), 70, "apply feedback");
        expectEqual (paramInt (proc.apvts, "pan"), 64, "apply pan");
    }

    {
        DelayProcessor proc;
        expectString (proc.importRecall (kTriplet), {}, "import triplet");
        expectEqual (paramInt (proc.apvts, "mix"), 10, "apply triplet mix");
        expectEqual (paramInt (proc.apvts, "mode"), 0, "apply mode triplet");
        expectEqual (paramInt (proc.apvts, "steps"), 3, "apply triplet steps");
        expectEqual (paramInt (proc.apvts, "feedback"), 40, "apply triplet feedback");
        expectEqual (paramInt (proc.apvts, "pan"), 0, "apply triplet pan");
    }

    {
        DelayProcessor proc;
        const int mix = paramInt (proc.apvts, "mix");
        const int steps = paramInt (proc.apvts, "steps");
        expectString (proc.importRecall ("openrb-pcf 1\nfrequency: 1\n"),
                      "not OpenRB Delay data",
                      "cross plugin");
        expectEqual (paramInt (proc.apvts, "mix"), mix, "no mix change on fail");
        expectEqual (paramInt (proc.apvts, "steps"), steps, "no steps change on fail");
    }

    if (gFails != 0)
    {
        std::cerr << gFails << " failure(s)\n";
        return 1;
    }

    std::cout << "All recall import tests passed\n";
    return 0;
}
