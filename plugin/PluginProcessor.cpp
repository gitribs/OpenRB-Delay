#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RecallImport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kEngineRate = 44100.0;
constexpr int kDelayBlock = 64;
constexpr int kPrintPpq = 192;
constexpr const char* kPrintIds[] = { "mix", "steps", "mode", "feedback", "pan", "enabled" };
constexpr int kPrintCount = (int) (sizeof kPrintIds / sizeof kPrintIds[0]);
static_assert (kPrintCount == 6, "print ids must match DelayProcessor::kPrintParams");
constexpr int kEnabledPrintIndex = 5;
static_assert (sizeof (OrbDelayParams) == 7, "OrbDelayParams ABI: steps, mode, feedback, pan, mix, bypass, enabled");

int printIndexForId (const char* id)
{
    if (id == nullptr)
        return -1;
    for (int i = 0; i < kPrintCount; ++i)
        if (std::strcmp (id, kPrintIds[i]) == 0)
            return i;
    return -1;
}

// Starting values for a newly inserted effect. Hosts restore project and preset
// state through setStateInformation, which replaces these after construction.
constexpr int kDefaultSteps = 3;
constexpr int kDefaultMode = 1; // Sixteenth
constexpr int kDefaultFeedback = 64;
constexpr int kDefaultPan = 64;
constexpr int kDefaultMix = 0;

bool ratesMatch (double a, double b)
{
    return std::abs (a - b) < 1.0e-6;
}
} // namespace

DelayProcessor::DelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      delay (orb_delay_new())
{
}

DelayProcessor::~DelayProcessor()
{
    cancelPendingUpdate();
    endPrintGestures();
}

juce::AudioProcessorValueTreeState::ParameterLayout DelayProcessor::createLayout()
{
    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterInt;
    using juce::ParameterID;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "steps", 1 }, "Steps", 1, 32, kDefaultSteps));
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Triplet", "Sixteenth" }, kDefaultMode));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "feedback", 1 }, "Feedback", 0, 127, kDefaultFeedback));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "pan", 1 }, "Pan", 0, 127, kDefaultPan));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "mix", 1 }, "Mix", 0, 127, kDefaultMix));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "enabled", 1 }, "On", true));
    return { p.begin(), p.end() };
}

void DelayProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate > 0.0 ? sampleRate : kEngineRate;
    exact44100 = ratesMatch (hostRate, kEngineRate);
    maxHostBlock = std::max (samplesPerBlock, 1);

    const int extra = 256 + (int) juce::WindowedSincInterpolator::getBaseLatency();
    const int maxEngine = (int) std::ceil ((double) maxHostBlock * kEngineRate / std::min (hostRate, kEngineRate))
                          + extra + kDelayBlock;

    hostFifo.setSize (2, maxHostBlock + extra);
    engineBuf.setSize (2, maxEngine);
    engineFifo.setSize (2, maxEngine + extra);
    monoScratch.setSize (1, maxEngine);
    hostFifo.clear();
    engineBuf.clear();
    engineFifo.clear();
    monoScratch.clear();
    hostFifoFill = 0;
    engineFifoFill = 0;

    for (auto& i : toEngine)
        i.reset();
    for (auto& i : toHost)
        i.reset();

    if (delay)
        orb_delay_reset (delay.get());

    setLatencySamples (latencySamples());
}

void DelayProcessor::releaseResources() {}

void DelayProcessor::reset()
{
    hostFifo.clear();
    engineBuf.clear();
    engineFifo.clear();
    hostFifoFill = 0;
    engineFifoFill = 0;
    for (auto& i : toEngine)
        i.reset();
    for (auto& i : toHost)
        i.reset();
    if (delay)
        orb_delay_reset (delay.get());
}

int DelayProcessor::latencySamples() const
{
    const int delayLatency = (int) std::llround ((double) kDelayBlock * hostRate / kEngineRate);
    if (exact44100)
        return delayLatency;

    const double base = juce::WindowedSincInterpolator::getBaseLatency();
    const double upHost = base;
    const double downHost = base * hostRate / kEngineRate;
    return delayLatency + (int) std::llround (upHost + downHost);
}

bool DelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

OrbDelayParams DelayProcessor::readParams (bool bypass) const
{
    auto asInt = [this] (const char* id, int fallback) -> int
    {
        if (auto* v = apvts.getRawParameterValue (id))
            return juce::roundToInt (v->load());
        return fallback;
    };

    OrbDelayParams params {};
    params.steps = (uint8_t) juce::jlimit (1, 32, asInt ("steps", kDefaultSteps));
    params.mode = asInt ("mode", kDefaultMode) == 0 ? 0 : 1;
    params.feedback = (uint8_t) juce::jlimit (0, 127, asInt ("feedback", kDefaultFeedback));
    params.pan = (uint8_t) juce::jlimit (0, 127, asInt ("pan", kDefaultPan));
    params.mix = (uint8_t) juce::jlimit (0, 127, asInt ("mix", kDefaultMix));
    params.bypass = bypass ? 1 : 0;
    params.enabled = (uint8_t) juce::jlimit (0, 1, asInt ("enabled", 1));
    return params;
}

OrbDelayPlayhead DelayProcessor::readPlayhead() const
{
    OrbDelayPlayhead head { 120.0 };
    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            head.bpm = info->getBpm().orFallback (120.0);
            if (!(head.bpm > 0.0) || !std::isfinite (head.bpm))
                head.bpm = 120.0;
        }
    }
    return head;
}

void DelayProcessor::shiftFifo (juce::AudioBuffer<float>& fifo, int& fill, int used)
{
    if (used <= 0)
        return;
    if (used >= fill)
    {
        fill = 0;
        return;
    }
    const int remain = fill - used;
    for (int ch = 0; ch < fifo.getNumChannels(); ++ch)
        std::memmove (fifo.getWritePointer (ch), fifo.getReadPointer (ch) + used, (size_t) remain * sizeof (float));
    fill = remain;
}

int DelayProcessor::hostToEngine (const float* left, const float* right, int nHost)
{
    const int cap = hostFifo.getNumSamples();
    const int take = juce::jmin (nHost, cap - hostFifoFill);
    if (take > 0)
    {
        std::memcpy (hostFifo.getWritePointer (0) + hostFifoFill, left, (size_t) take * sizeof (float));
        std::memcpy (hostFifo.getWritePointer (1) + hostFifoFill, right, (size_t) take * sizeof (float));
        hostFifoFill += take;
    }

    const double speed = hostRate / kEngineRate;
    int nEngine = (int) std::floor ((double) hostFifoFill / speed);
    nEngine = juce::jlimit (0, engineBuf.getNumSamples(), nEngine);
    if (nEngine <= 0)
        return 0;

    int used = 0;
    for (int ch = 0; ch < 2; ++ch)
        used = toEngine[ch].process (speed, hostFifo.getReadPointer (ch), engineBuf.getWritePointer (ch), nEngine);

    shiftFifo (hostFifo, hostFifoFill, used);
    return nEngine;
}

void DelayProcessor::engineToHost (float* left, float* right, int nHost, int nEngine)
{
    const int cap = engineFifo.getNumSamples();
    const int take = juce::jmin (nEngine, cap - engineFifoFill);
    if (take > 0)
    {
        std::memcpy (engineFifo.getWritePointer (0) + engineFifoFill, engineBuf.getReadPointer (0),
                     (size_t) take * sizeof (float));
        std::memcpy (engineFifo.getWritePointer (1) + engineFifoFill, engineBuf.getReadPointer (1),
                     (size_t) take * sizeof (float));
        engineFifoFill += take;
    }

    const double speed = kEngineRate / hostRate;
    float* outs[2] = { left, right };
    int used = 0;
    for (int ch = 0; ch < 2; ++ch)
        used = toHost[ch].process (speed, engineFifo.getReadPointer (ch), outs[ch], nHost, engineFifoFill, 0);

    shiftFifo (engineFifo, engineFifoFill, juce::jmin (used, engineFifoFill));
}

void DelayProcessor::processBuffer (juce::AudioBuffer<float>& buffer, bool bypass)
{
    runPrintTake();
    juce::ScopedNoDenormals noDenormals;

    const int nHost = buffer.getNumSamples();
    const int nCh = buffer.getNumChannels();
    if (nHost <= 0 || nCh <= 0 || delay == nullptr)
        return;

    for (int ch = nCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, nHost);

    float* left = buffer.getWritePointer (0);
    float* right = nCh > 1 ? buffer.getWritePointer (1) : monoScratch.getWritePointer (0);
    if (nCh == 1)
        std::memcpy (right, left, (size_t) nHost * sizeof (float));

    const auto params = readParams (bypass);
    const auto head = readPlayhead();

    if (exact44100)
    {
        orb_delay_process (delay.get(), left, right, nHost, &params, &head);
        if (nCh == 1)
            std::memcpy (buffer.getWritePointer (0), left, (size_t) nHost * sizeof (float));
        return;
    }

    const int nEngine = hostToEngine (left, right, nHost);
    if (nEngine > 0)
        orb_delay_process (delay.get(), engineBuf.getWritePointer (0), engineBuf.getWritePointer (1), nEngine, &params, &head);
    engineToHost (left, right, nHost, nEngine);
    if (nCh == 1)
        std::memcpy (buffer.getWritePointer (0), left, (size_t) nHost * sizeof (float));
}

void DelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    processBuffer (buffer, false);
}

void DelayProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    processBuffer (buffer, true);
}

juce::String DelayProcessor::importRecall (const juce::String& text)
{
    DelayRecall parsed;
    const auto err = parseRecall (text, parsed);
    if (err.isNotEmpty())
        return err;

    DelayPrint printed;
    parsePrint (text, printed);

    applyRecall (apvts, parsed);

    if (printed.events.empty())
    {
        clearPrintTake();
        return {};
    }

    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        printEvents = std::move (printed.events);
        printLoaded = true;
    }
    armPrint();
    return {};
}

std::vector<DelayPrintEvent> DelayProcessor::copyPrintEvents() const
{
    const juce::SpinLock::ScopedLockType sl (printLock);
    return printEvents;
}

juce::RangedAudioParameter* DelayProcessor::printParam (int index) const
{
    if (index < 0 || index >= kPrintParams)
        return nullptr;
    return apvts.getParameter (kPrintIds[index]);
}

void DelayProcessor::endPrintGestures()
{
    for (int i = 0; i < kPrintParams; ++i)
    {
        if (! printGesturing[(size_t) i])
            continue;
        printGesturing[(size_t) i] = false;
        if (auto* p = printParam (i))
            p->endChangeGesture();
    }
}

bool DelayProcessor::printValueAt (const std::vector<DelayPrintEvent>& events,
                                   int index, int tick, int& value)
{
    if (index < 0 || index >= kPrintParams)
        return false;
    bool found = false;
    for (const auto& e : events)
    {
        if (printIndexForId (e.id) != index || e.tick > tick)
            continue;
        found = true;
        value = e.value;
    }
    return found;
}

void DelayProcessor::setPrintParam (int index, int value)
{
    auto* p = printParam (index);
    if (p == nullptr)
        return;
    p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}

void DelayProcessor::writePrintParam (int index, int value)
{
    beginPrintGesture (index);
    setPrintParam (index, value);
    if (index != kEnabledPrintIndex || ! printGesturing[(size_t) index])
        return;
    printGesturing[(size_t) index] = false;
    if (auto* p = printParam (index))
        p->endChangeGesture();
}

void DelayProcessor::beginPrintGesture (int index)
{
    if (index < 0 || index >= kPrintParams || printGesturing[(size_t) index])
        return;
    if (auto* p = printParam (index))
    {
        p->beginChangeGesture();
        printGesturing[(size_t) index] = true;
    }
}

void DelayProcessor::seedPrintOrigin()
{
    std::vector<DelayPrintEvent> events;
    std::array<bool, kPrintParams> moving {};
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        events = printEvents;
        moving = printMoving;
    }

    for (int i = 0; i < kPrintParams; ++i)
    {
        if (! moving[(size_t) i])
            continue;
        int value = 0;
        const bool haveOrigin = printValueAt (events, i, 0, value);
        if (i == kEnabledPrintIndex)
        {
            if (haveOrigin)
                writePrintParam (i, value);
            continue;
        }
        beginPrintGesture (i);
        if (haveOrigin)
            setPrintParam (i, value);
    }
}

void DelayProcessor::armPrint()
{
    if (wrapperType == wrapperType_Standalone)
        return;

    endPrintGestures();
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        if (printEvents.empty())
        {
            printArmed = false;
            printLoaded = false;
            return;
        }

        printMoving.fill (false);
        bool haveEnabled = false;
        for (const auto& e : printEvents)
        {
            const int i = printIndexForId (e.id);
            if (i < 0)
                continue;
            if (e.tick > 0)
                printMoving[(size_t) i] = true;
            if (i == kEnabledPrintIndex)
                haveEnabled = true;
        }
        if (haveEnabled)
            printMoving[(size_t) kEnabledPrintIndex] = true;
        printPendingOn.fill (false);
        printTakeActive = false;
        printLastTick = 0;
        havePrintPpq = false;
        printStopPending = false;
        printLoaded = true;
    }

    // Write tick-0 values while stopped so Latch/Write capture bar 1. Async updates
    // during the take are stamped at the message-thread playhead and can miss the start.
    seedPrintOrigin();

    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        printArmed = true;
    }
}

void DelayProcessor::disarmPrint()
{
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        printArmed = false;
        printTakeActive = false;
        printStopPending = false;
        printPendingOn.fill (false);
    }
    endPrintGestures();
}

void DelayProcessor::clearPrintTake()
{
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        printArmed = false;
        printTakeActive = false;
        printStopPending = false;
        printEvents.clear();
        printMoving.fill (false);
        printPendingOn.fill (false);
        printLoaded = false;
        havePrintPpq = false;
        printLastTick = 0;
    }
    endPrintGestures();
}

bool DelayProcessor::setPrintArmed (bool on)
{
    if (wrapperType == wrapperType_Standalone)
        return false;
    if (on)
        armPrint();
    else
        disarmPrint();
    return isPrintArmed();
}

void DelayProcessor::queuePrintValue (int index, int value)
{
    if (index < 0 || index >= kPrintParams)
        return;
    printPending[(size_t) index] = value;
    printPendingOn[(size_t) index] = true;
}

void DelayProcessor::chasePrintAt (int tick)
{
    for (int i = 0; i < kPrintParams; ++i)
    {
        if (! printMoving[(size_t) i])
            continue;
        int value = 0;
        if (printValueAt (printEvents, i, tick, value))
            queuePrintValue (i, value);
    }
}

void DelayProcessor::emitPrintRange (int fromTick, int toTick)
{
    for (const auto& e : printEvents)
    {
        if (e.tick <= fromTick || e.tick > toTick)
            continue;
        const int index = printIndexForId (e.id);
        if (index >= 0 && printMoving[(size_t) index])
            queuePrintValue (index, e.value);
    }
}

void DelayProcessor::runPrintTake()
{
    if (wrapperType == wrapperType_Standalone || ! printArmed.load())
        return;

    bool hostPlaying = false;
    bool hasPpq = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            hostPlaying = info->getIsPlaying() || info->getIsRecording();
            if (const auto pos = info->getPpqPosition(); pos.hasValue() && std::isfinite (*pos))
            {
                ppq = *pos;
                hasPpq = true;
            }
        }
    }

    bool notify = false;
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        if (! printArmed.load())
            return;

        if (hasPpq && havePrintPpq && std::abs (ppq - printLastPpq) > 1.0e-9)
            hostPlaying = true;
        if (hasPpq)
        {
            printLastPpq = ppq;
            havePrintPpq = true;
        }

        if (! hostPlaying)
        {
            if (printTakeActive)
            {
                printTakeActive = false;
                printArmed = false;
                printStopPending = true;
                notify = true;
            }
        }
        else if (hasPpq)
        {
            const int tick = (int) std::llround (ppq * (double) kPrintPpq);
            if (! printTakeActive || tick < printLastTick)
                chasePrintAt (tick);
            else
                emitPrintRange (printLastTick, tick);
            printTakeActive = true;
            printLastTick = tick;
            for (bool on : printPendingOn)
            {
                if (on)
                {
                    notify = true;
                    break;
                }
            }
        }
    }

    if (notify)
        triggerAsyncUpdate();
}

void DelayProcessor::handleAsyncUpdate()
{
    std::array<int, kPrintParams> values {};
    std::array<bool, kPrintParams> on {};
    bool stop = false;
    {
        const juce::SpinLock::ScopedLockType sl (printLock);
        values = printPending;
        on = printPendingOn;
        printPendingOn.fill (false);
        stop = printStopPending.exchange (false);
    }

    for (int i = 0; i < kPrintParams; ++i)
    {
        if (! on[(size_t) i])
            continue;
        if (i == kEnabledPrintIndex)
            writePrintParam (i, values[(size_t) i]);
        else
        {
            beginPrintGesture (i);
            setPrintParam (i, values[(size_t) i]);
        }
    }

    if (stop)
    {
        {
            const juce::SpinLock::ScopedLockType sl (printLock);
            printPendingOn.fill (false);
            printTakeActive = false;
            printArmed = false;
        }
        endPrintGestures();
    }
}

juce::AudioProcessorEditor* DelayProcessor::createEditor()
{
    return new DelayEditor (*this);
}

void DelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DelayProcessor();
}
