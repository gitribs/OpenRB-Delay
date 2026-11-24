#pragma once

#include <JuceHeader.h>
#include "orb_delay.h"
#include "RecallImport.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class DelayEditor;

class DelayProcessor final : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    DelayProcessor();
    ~DelayProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::String importRecall (const juce::String& text);

    bool setPrintArmed (bool on);
    bool isPrintArmed() const noexcept { return printArmed.load(); }
    bool hasPrintTake() const noexcept { return printLoaded.load(); }
    std::vector<DelayPrintEvent> copyPrintEvents() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    struct OrbDelayDeleter
    {
        void operator() (OrbDelay* p) const noexcept { orb_delay_free (p); }
    };

    static constexpr int kPrintParams = 6;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processBuffer (juce::AudioBuffer<float>& buffer, bool bypass);
    OrbDelayParams readParams (bool bypass) const;
    OrbDelayPlayhead readPlayhead() const;
    int latencySamples() const;
    int hostToEngine (const float* left, const float* right, int nHost);
    void engineToHost (float* left, float* right, int nHost, int nEngine);
    void shiftFifo (juce::AudioBuffer<float>& fifo, int& fill, int used);

    void handleAsyncUpdate() override;
    void armPrint();
    void disarmPrint();
    void clearPrintTake();
    void endPrintGestures();
    void runPrintTake();
    void queuePrintValue (int index, int value);
    void chasePrintAt (int tick);
    void emitPrintRange (int fromTick, int toTick);
    void setPrintParam (int index, int value);
    void writePrintParam (int index, int value);
    void beginPrintGesture (int index);
    void seedPrintOrigin();
    juce::RangedAudioParameter* printParam (int index) const;
    static bool printValueAt (const std::vector<DelayPrintEvent>& events,
                              int index, int tick, int& value);

    std::unique_ptr<OrbDelay, OrbDelayDeleter> delay;
    double hostRate = 44100.0;
    bool exact44100 = true;
    int maxHostBlock = 0;

    juce::WindowedSincInterpolator toEngine[2];
    juce::WindowedSincInterpolator toHost[2];
    juce::AudioBuffer<float> hostFifo;
    juce::AudioBuffer<float> engineBuf;
    juce::AudioBuffer<float> engineFifo;
    juce::AudioBuffer<float> monoScratch;
    int hostFifoFill = 0;
    int engineFifoFill = 0;

    mutable juce::SpinLock printLock;
    std::vector<DelayPrintEvent> printEvents;
    std::array<bool, kPrintParams> printMoving {};
    std::array<int, kPrintParams> printPending {};
    std::array<bool, kPrintParams> printPendingOn {};
    std::array<bool, kPrintParams> printGesturing {};
    std::atomic<bool> printArmed { false };
    std::atomic<bool> printStopPending { false };
    std::atomic<bool> printLoaded { false };
    bool printTakeActive = false;
    bool havePrintPpq = false;
    double printLastPpq = 0.0;
    int printLastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayProcessor)
};
