#pragma once

#include "PluginProcessor.h"

#include <memory>

class DelayLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DelayLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override;
    juce::Font getLabelFont (juce::Label&) override;
};

class GearButton final : public juce::Button
{
public:
    GearButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class RecallImportPanel;

class DelayEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DelayEditor (DelayProcessor&);
    ~DelayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setupKnob (juce::Slider& s);
    void setupIncDec (juce::Slider& s);
    void setupModeButtons (DelayProcessor& p);
    void setImportOpen (bool open);

    DelayProcessor& owner;
    DelayLookAndFeel lnf;

    juce::Slider mix, feedback, pan, steps;
    juce::ToggleButton triplet { "Triplet" }, sixteenth { "Sixteenth" };
    juce::Label title, mixN, feedbackN, panN, modeN, stepsN;
    GearButton gear;
    std::unique_ptr<RecallImportPanel> importPanel;

    juce::AudioProcessorValueTreeState::SliderAttachment mixA, feedbackA, panA, stepsA;
    std::unique_ptr<juce::ParameterAttachment> modeA;
    bool ignoreModeCallbacks = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayEditor)
};
