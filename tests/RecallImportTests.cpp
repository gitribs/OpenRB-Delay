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

void expectTrue (bool v, const char* msg)
{
    if (! v)
    {
        std::cerr << "FAIL: " << msg << "\n";
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

const juce::String kWithPrint =
    "openrb-delay 1\n"
    "mix: 22\n"
    "steps: 8\n"
    "mode: sixteenth\n"
    "feedback: 70\n"
    "pan: 64\n"
    "\n"
    "openrb-print 1\n"
    "ppq: 192\n"
    "0 mix 22\n"
    "768 mix 40\n"
    "1536 feedback 90\n";
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
        expectEqual (rec.enabled, 1, "missing enabled defaults on");
    }

    {
        DelayRecall rec;
        expectString (parseRecall (kTriplet, rec), {}, "parse triplet");
        expectEqual (rec.mix, 10, "triplet mix");
        expectEqual (rec.mode, 0, "triplet mode");
        expectEqual (rec.enabled, 1, "triplet enabled default");
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
        expectEqual (paramInt (proc.apvts, "enabled"), 1, "default enabled on");
        expectString (proc.importRecall (kModloader), {}, "import modloader");
        expectEqual (paramInt (proc.apvts, "mix"), 22, "apply mix");
        expectEqual (paramInt (proc.apvts, "steps"), 8, "apply steps");
        expectEqual (paramInt (proc.apvts, "mode"), 1, "apply mode sixteenth");
        expectEqual (paramInt (proc.apvts, "feedback"), 70, "apply feedback");
        expectEqual (paramInt (proc.apvts, "pan"), 64, "apply pan");
        expectEqual (paramInt (proc.apvts, "enabled"), 1, "apply enabled default");
        auto* enabled = proc.apvts.getParameter ("enabled");
        expectTrue (enabled != nullptr, "enabled param exists");
        expectTrue (enabled->convertTo0to1 (0.0f) == 0.0f, "bool convert 0");
        expectTrue (enabled->convertTo0to1 (1.0f) == 1.0f, "bool convert 1");
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

    {
        DelayRecall rec;
        expectString (parseRecall (kWithPrint, rec), {}, "recall with print appendix");
        expectEqual (rec.mix, 22, "appendix recall mix");
        expectEqual (rec.feedback, 70, "appendix recall feedback");

        DelayPrint printed;
        expectString (parsePrint (kWithPrint, printed), {}, "parse print appendix");
        expectEqual (printed.ppq, 192, "print ppq");
        expectEqual ((int) printed.events.size(), 3, "print event count");
        expectEqual (printed.events[0].tick, 0, "print tick 0");
        expectString (juce::String (printed.events[0].id), "mix", "print id 0 mix");
        expectEqual (printed.events[0].value, 22, "print mix 22");
        expectEqual (printed.events[1].tick, 768, "print tick mix 40");
        expectString (juce::String (printed.events[1].id), "mix", "print id 1 mix");
        expectEqual (printed.events[1].value, 40, "print mix 40");
        expectEqual (printed.events[2].tick, 1536, "print tick feedback");
        expectString (juce::String (printed.events[2].id), "feedback", "print id feedback");
        expectEqual (printed.events[2].value, 90, "print feedback 90");
    }

    {
        DelayPrint printed;
        expectString (parsePrint (kModloader, printed), {}, "recall-only print");
        expectEqual ((int) printed.events.size(), 0, "recall-only empty events");
        expectEqual (printed.ppq, 192, "recall-only ppq");
    }

    {
        DelayPrint printed;
        const juce::String unknown =
            kModloader
            + "\nopenrb-print 1\n"
              "ppq: 192\n"
              "0 mix 22\n"
              "0 enabled 1\n"
              "768 mix 40\n"
              "100 banana 3\n";
        expectString (parsePrint (unknown, printed), {}, "unknown print params");
        expectEqual ((int) printed.events.size(), 3, "keep enabled, skip banana");
        expectString (juce::String (printed.events[0].id), "mix", "kept mix start");
        expectString (juce::String (printed.events[1].id), "enabled", "kept enabled");
        expectEqual (printed.events[1].value, 1, "enabled value 1");
        expectString (juce::String (printed.events[2].id), "mix", "kept mix move");
        expectEqual (printed.events[2].tick, 768, "kept mix tick");
    }

    {
        DelayRecall rec;
        DelayPrint printed;
        const juce::String v2 =
            kModloader
            + "\nopenrb-print 2\n"
              "ppq: 192\n"
              "0 mix 22\n";
        expectString (parseRecall (v2, rec), {}, "recall still ok on print v2");
        expectEqual (rec.mix, 22, "print v2 mix snapshot");
        expectString (parsePrint (v2, printed), {}, "print v2 ignored");
        expectEqual ((int) printed.events.size(), 0, "print v2 empty");
    }

    {
        DelayPrint printed;
        const juce::String scaled =
            kModloader
            + "\nopenrb-print 1\n"
              "ppq: 96\n"
              "0 mix 22\n"
              "384 mix 40\n";
        expectString (parsePrint (scaled, printed), {}, "scale ppq");
        expectEqual (printed.ppq, 192, "scaled ppq is 192");
        expectEqual ((int) printed.events.size(), 2, "scaled events");
        expectEqual (printed.events[1].tick, 768, "tick scaled 96 to 192");
    }

    {
        DelayProcessor proc;
        expectString (proc.importRecall (kWithPrint), {}, "import with print");
        expectTrue (proc.isPrintArmed(), "import arms print");
        expectEqual (paramInt (proc.apvts, "mix"), 22, "import print mix");
        expectEqual (paramInt (proc.apvts, "feedback"), 70, "import print feedback");
        const auto events = proc.copyPrintEvents();
        expectEqual ((int) events.size(), 3, "import stored print events");
        expectTrue (proc.hasPrintTake(), "import loaded print take");
        expectEqual (events[0].tick, 0, "stored tick 0");
        expectString (juce::String (events[0].id), "mix", "stored mix");
        expectEqual (events[2].value, 90, "stored feedback value");

        expectString (proc.importRecall (kModloader), {}, "import recall-only after print");
        expectEqual ((int) proc.copyPrintEvents().size(), 0, "recall-only clears print");
        expectTrue (! proc.hasPrintTake(), "recall-only unloads print");
        expectTrue (! proc.isPrintArmed(), "recall-only disarms print");
        expectEqual (paramInt (proc.apvts, "mix"), 22, "recall-only mix stays");
        expectEqual (paramInt (proc.apvts, "enabled"), 1, "recall-only enabled stays on");
    }

    {
        DelayRecall rec;
        const juce::String off =
            "openrb-delay 1\n"
            "mix: 22\n"
            "steps: 8\n"
            "mode: sixteenth\n"
            "feedback: 70\n"
            "pan: 64\n"
            "enabled: 0\n";
        expectString (parseRecall (off, rec), {}, "parse enabled 0");
        expectEqual (rec.enabled, 0, "snapshot enabled 0");

        DelayProcessor proc;
        expectString (proc.importRecall (off), {}, "import enabled 0");
        expectEqual (paramInt (proc.apvts, "enabled"), 0, "apply enabled off");
        expectString (proc.importRecall (kModloader), {}, "import missing enabled");
        expectEqual (paramInt (proc.apvts, "enabled"), 1, "missing enabled restores on");
    }

    {
        DelayRecall rec;
        const juce::String clamped =
            "openrb-delay 1\n"
            "mix: 10\n"
            "steps: 3\n"
            "mode: sixteenth\n"
            "feedback: 40\n"
            "pan: 0\n"
            "enabled: 5\n";
        expectString (parseRecall (clamped, rec), {}, "clamp enabled");
        expectEqual (rec.enabled, 1, "enabled 5 clamps to 1");
    }

    {
        DelayRecall rec;
        expectString (parseRecall (
                          "openrb-delay 1\n"
                          "mix: 1\n"
                          "steps: 1\n"
                          "mode: sixteenth\n"
                          "feedback: 0\n"
                          "pan: 0\n"
                          "enabled: banana\n",
                          rec),
                      "invalid enabled",
                      "bad enabled");
    }

    {
        const juce::String tick0 =
            "openrb-delay 1\n"
            "mix: 22\n"
            "steps: 8\n"
            "mode: sixteenth\n"
            "feedback: 70\n"
            "pan: 64\n"
            "enabled: 1\n"
            "\n"
            "openrb-print 1\n"
            "ppq: 192\n"
            "0 enabled 0\n";
        DelayPrint printed;
        expectString (parsePrint (tick0, printed), {}, "parse tick-0 enabled");
        expectEqual ((int) printed.events.size(), 1, "tick-0 enabled event");
        expectString (juce::String (printed.events[0].id), "enabled", "tick-0 id");
        expectEqual (printed.events[0].tick, 0, "tick-0");
        expectEqual (printed.events[0].value, 0, "tick-0 value off");

        DelayProcessor proc;
        expectString (proc.importRecall (tick0), {}, "import tick-0 enabled");
        expectTrue (proc.hasPrintTake(), "tick-0 enable loads print");
        expectTrue (proc.isPrintArmed(), "tick-0 enable arms print");
        const auto events = proc.copyPrintEvents();
        expectEqual ((int) events.size(), 1, "stored tick-0 enable");
        expectString (juce::String (events[0].id), "enabled", "stored enable id");
        // Always-lane: seed writes tick 0 even though nothing moves later.
        expectEqual (paramInt (proc.apvts, "enabled"), 0, "tick-0 enable is a moving lane");
    }

    if (gFails != 0)
    {
        std::cerr << gFails << " failure(s)\n";
        return 1;
    }

    std::cout << "All recall import tests passed\n";
    return 0;
}
