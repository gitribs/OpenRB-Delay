#include "RecallImport.h"

#include <cmath>
#include <cstring>

namespace
{
constexpr int kPrintPpq = 192;

bool parseIntValue (const juce::String& s, int& out)
{
    const auto t = s.trim();
    if (t.isEmpty())
        return false;

    int i = 0;
    if (t[0] == '-' || t[0] == '+')
        ++i;
    if (i >= t.length())
        return false;

    for (int n = t.length(); i < n; ++i)
        if (! juce::CharacterFunctions::isDigit (t[i]))
            return false;

    out = t.getIntValue();
    return true;
}

void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float actual)
{
    auto* p = apvts.getParameter (id);
    if (p == nullptr)
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (actual));
    p->endChangeGesture();
}

const char* internPrintId (const juce::String& s)
{
    static constexpr const char* kIds[] = { "mix", "steps", "mode", "feedback", "pan", "enabled" };
    for (auto* id : kIds)
        if (s == id)
            return id;
    return nullptr;
}

int clampPrintValue (const char* id, int n)
{
    if (std::strcmp (id, "steps") == 0)
        return juce::jlimit (1, 32, n);
    if (std::strcmp (id, "mode") == 0 || std::strcmp (id, "enabled") == 0)
        return juce::jlimit (0, 1, n);
    return juce::jlimit (0, 127, n);
}
} // namespace

juce::String parseRecall (const juce::String& text, DelayRecall& out)
{
    const auto lines = juce::StringArray::fromLines (text);
    int line = 0;
    while (line < lines.size() && lines[line].trim().isEmpty())
        ++line;
    if (line >= lines.size())
        return "not OpenRB Delay data";

    const auto header = juce::StringArray::fromTokens (lines[line].trim(), " \t", {});
    if (header.isEmpty() || header[0] != "openrb-delay")
        return "not OpenRB Delay data";
    if (header.size() < 2 || header[1] != "1")
        return "unsupported recall version";

    bool haveMix = false, haveSteps = false, haveMode = false, haveFb = false, havePan = false, haveEnabled = false;
    DelayRecall parsed;

    for (++line; line < lines.size(); ++line)
    {
        const auto trimmed = lines[line].trim();
        if (trimmed.isEmpty())
            continue;

        const int colon = trimmed.indexOfChar (':');
        if (colon < 0)
            continue;

        const auto key = trimmed.substring (0, colon).trim();
        const auto value = trimmed.substring (colon + 1).trim();

        auto takeInt = [&] (int lo, int hi, int& dest, bool& flag) -> juce::String
        {
            int n = 0;
            if (! parseIntValue (value, n))
                return "invalid " + key;
            dest = juce::jlimit (lo, hi, n);
            flag = true;
            return {};
        };

        juce::String err;
        if (key == "mix")
            err = takeInt (0, 127, parsed.mix, haveMix);
        else if (key == "steps")
            err = takeInt (1, 32, parsed.steps, haveSteps);
        else if (key == "feedback")
            err = takeInt (0, 127, parsed.feedback, haveFb);
        else if (key == "pan")
            err = takeInt (0, 127, parsed.pan, havePan);
        else if (key == "mode")
        {
            if (value == "triplet")
                parsed.mode = 0;
            else if (value == "sixteenth")
                parsed.mode = 1;
            else
                return "invalid mode";
            haveMode = true;
        }
        else if (key == "enabled")
            err = takeInt (0, 1, parsed.enabled, haveEnabled);

        if (err.isNotEmpty())
            return err;
    }

    if (! haveMix)
        return "missing mix";
    if (! haveSteps)
        return "missing steps";
    if (! haveMode)
        return "missing mode";
    if (! haveFb)
        return "missing feedback";
    if (! havePan)
        return "missing pan";

    juce::ignoreUnused (haveEnabled);

    out = parsed;
    return {};
}

void applyRecall (juce::AudioProcessorValueTreeState& apvts, const DelayRecall& values)
{
    setParam (apvts, "mix", (float) values.mix);
    setParam (apvts, "steps", (float) values.steps);
    setParam (apvts, "mode", (float) values.mode);
    setParam (apvts, "feedback", (float) values.feedback);
    setParam (apvts, "pan", (float) values.pan);
    setParam (apvts, "enabled", (float) values.enabled);
}

juce::String parsePrint (const juce::String& text, DelayPrint& out)
{
    out = {};
    out.ppq = kPrintPpq;

    const auto lines = juce::StringArray::fromLines (text);
    int line = 0;
    bool found = false;
    for (; line < lines.size(); ++line)
    {
        const auto trimmed = lines[line].trim();
        if (trimmed.isEmpty())
            continue;

        const auto header = juce::StringArray::fromTokens (trimmed, " \t", {});
        if (header.isEmpty() || header[0] != "openrb-print")
            continue;
        if (header.size() < 2 || header[1] != "1")
            return {};
        found = true;
        ++line;
        break;
    }

    if (! found)
        return {};

    int srcPpq = kPrintPpq;
    std::vector<DelayPrintEvent> events;

    for (; line < lines.size(); ++line)
    {
        const auto trimmed = lines[line].trim();
        if (trimmed.isEmpty())
            continue;

        const int colon = trimmed.indexOfChar (':');
        if (colon >= 0)
        {
            const auto key = trimmed.substring (0, colon).trim();
            const auto value = trimmed.substring (colon + 1).trim();
            if (key == "ppq")
            {
                int n = 0;
                if (parseIntValue (value, n) && n > 0)
                    srcPpq = n;
            }
            continue;
        }

        const auto tok = juce::StringArray::fromTokens (trimmed, " \t", {});
        if (tok.size() != 3)
            continue;

        int tick = 0, value = 0;
        if (! parseIntValue (tok[0], tick) || ! parseIntValue (tok[2], value))
            continue;

        const char* id = internPrintId (tok[1]);
        if (id == nullptr)
            continue;

        events.push_back ({ tick, id, clampPrintValue (id, value) });
    }

    if (srcPpq != kPrintPpq)
    {
        for (auto& e : events)
            e.tick = (int) std::llround ((double) e.tick * (double) kPrintPpq / (double) srcPpq);
    }

    out.ppq = kPrintPpq;
    out.events = std::move (events);
    return {};
}
