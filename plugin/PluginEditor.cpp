#include "PluginEditor.h"

#include <functional>

namespace
{
const juce::Colour kBg { 0xff161618 };
const juce::Colour kPanel { 0xff242428 };
const juce::Colour kAmber { 0xffe0a020 };
const juce::Colour kText { 0xffe6e4dc };
const juce::Colour kMuted { 0xff8a8680 };
const juce::Colour kWell { 0xff1a1a1c };
const juce::Colour kLine { 0xff3a3a40 };
const juce::Colour kError { 0xffd06060 };
constexpr int kModeRadioGroup = 1;
constexpr int kEditorW = 560;
constexpr int kEditorH = 240;
constexpr int kGearSize = 24;

class IncDecArrowButton final : public juce::Button
{
public:
    explicit IncDecArrowButton (bool up) : juce::Button ({}), isUp (up) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (kWell);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (highlighted || down ? kAmber : kLine);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);

        juce::Path p;
        const auto c = r.getCentre();
        constexpr float w = 5.0f;
        constexpr float h = 3.6f;
        if (isUp)
            p.addTriangle (c.x, c.y - h, c.x + w, c.y + h * 0.55f, c.x - w, c.y + h * 0.55f);
        else
            p.addTriangle (c.x, c.y + h, c.x + w, c.y - h * 0.55f, c.x - w, c.y - h * 0.55f);

        g.setColour (down ? kAmber : kText);
        g.fillPath (p);
    }

private:
    bool isUp;
};
} // namespace

DelayLookAndFeel::DelayLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kAmber);
    setColour (juce::ToggleButton::tickDisabledColourId, kLine);
    setColour (juce::CaretComponent::caretColourId, kAmber);
    setColour (juce::TextEditor::backgroundColourId, kWell);
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::outlineColourId, kLine);
    setColour (juce::TextEditor::focusedOutlineColourId, kAmber);
    setColour (juce::TextEditor::highlightColourId, kAmber);
    setColour (juce::TextEditor::highlightedTextColourId, kBg);
    setColour (juce::TextButton::buttonColourId, kWell);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, kText);
}

void DelayLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    g.setColour (kWell);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (kLine);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.4f);

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (kAmber);
    g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path tick;
    tick.addRectangle (-1.1f, -radius + 8.0f, 2.2f, radius * 0.42f);
    g.setColour (kText);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void DelayLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                   float x, float y, float w, float h,
                                   bool ticked, bool isEnabled, bool, bool)
{
    auto bounds = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
    g.setColour ((isEnabled ? kLine : kLine.withAlpha (0.5f)));

    auto* button = dynamic_cast<juce::Button*> (&component);
    const bool radio = (button != nullptr && button->getRadioGroupId() != 0)
                       || (bool) component.getProperties()["radioLook"];
    if (radio)
    {
        g.drawEllipse (bounds, 1.4f);
        if (ticked)
        {
            g.setColour (isEnabled ? kAmber : kAmber.withAlpha (0.5f));
            g.fillEllipse (bounds.reduced (3.4f));
        }
        return;
    }

    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    if (ticked)
    {
        g.setColour (isEnabled ? kAmber : kAmber.withAlpha (0.5f));
        auto tick = getTickShape (0.75f);
        g.fillPath (tick, tick.getTransformToScaleToFit (bounds.reduced (3.0f, 4.0f), false));
    }
}

void DelayLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kWell);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (highlighted || down ? kAmber : kLine);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

juce::Button* DelayLookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    return new IncDecArrowButton (isIncrement);
}

juce::Font DelayLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

GearButton::GearButton() : juce::Button ({})
{
    setName ("Import recall");
    setTooltip ("Import recall");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GearButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kWell);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (highlighted || down ? kAmber : kLine);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);

    const auto c = r.getCentre();
    constexpr float outer = 6.6f;
    constexpr float inner = 4.2f;
    constexpr float hole = 1.8f;
    constexpr float toothW = 2.3f;
    g.setColour (down ? kAmber : kText);
    juce::Path gear;
    for (int i = 0; i < 4; ++i)
    {
        juce::Path tooth;
        tooth.addRoundedRectangle (-toothW * 0.5f, -outer, toothW, outer * 2.0f, 0.45f);
        gear.addPath (tooth, juce::AffineTransform::rotation ((float) i * juce::MathConstants<float>::pi * 0.25f)
                                 .translated (c.x, c.y));
    }
    gear.addEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);
    g.fillPath (gear);
    g.setColour (kWell);
    g.fillEllipse (c.x - hole, c.y - hole, hole * 2.0f, hole * 2.0f);
}

class RecallImportPanel final : public juce::Component
{
public:
    RecallImportPanel (DelayProcessor& p, std::function<void()> onCloseIn)
        : owner (p), onClose (std::move (onCloseIn))
    {
        heading.setText ("Import recall", juce::dontSendNotification);
        heading.setJustificationType (juce::Justification::centredLeft);
        heading.setColour (juce::Label::textColourId, kAmber);
        heading.setFont (juce::FontOptions().withHeight (16.0f).withStyle ("Bold"));
        addAndMakeVisible (heading);

        hint.setText ("Paste OpenRB Delay settings copied from Modloader.", juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setColour (juce::Label::textColourId, kMuted);
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);

        editor.setMultiLine (true);
        editor.setReturnKeyStartsNewLine (true);
        editor.setFont (juce::FontOptions (13.0f));
        editor.setTextToShowWhenEmpty ("openrb-delay 1\nmix: 22\nsteps: 8\nmode: sixteenth\n...", kMuted);
        addAndMakeVisible (editor);

        status.setJustificationType (juce::Justification::centredLeft);
        status.setColour (juce::Label::textColourId, kError);
        status.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (status);

        paste.setButtonText ("Paste");
        paste.onClick = [this]
        {
            editor.setText (juce::SystemClipboard::getTextFromClipboard());
            status.setText ({}, juce::dontSendNotification);
        };
        addAndMakeVisible (paste);

        import.setButtonText ("Import");
        import.onClick = [this]
        {
            const auto err = owner.importRecall (editor.getText());
            if (err.isEmpty())
            {
                if (onClose)
                    onClose();
                return;
            }
            status.setColour (juce::Label::textColourId, kError);
            status.setText (err, juce::dontSendNotification);
        };
        addAndMakeVisible (import);

        close.setButtonText ("Close");
        close.onClick = [this]
        {
            if (onClose)
                onClose();
        };
        addAndMakeVisible (close);
        setWantsKeyboardFocus (true);
    }

    void reset()
    {
        editor.clear();
        status.setText ({}, juce::dontSendNotification);
        editor.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBg.withAlpha (0.82f));
        auto card = cardBounds().toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (card, 8.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (card, 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = cardBounds().reduced (16);
        heading.setBounds (r.removeFromTop (22));
        hint.setBounds (r.removeFromTop (18));
        r.removeFromTop (8);
        auto buttons = r.removeFromBottom (28);
        r.removeFromBottom (8);
        status.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (8);
        editor.setBounds (r);

        paste.setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (8);
        import.setBounds (buttons.removeFromLeft (80));
        close.setBounds (buttons.removeFromRight (80));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardBounds().contains (e.getPosition()) && onClose)
            onClose();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<int> cardBounds() const
    {
        return getLocalBounds().reduced (16).withSizeKeepingCentre (
            juce::jmin (380, getWidth() - 32),
            juce::jmin (208, getHeight() - 32));
    }

    DelayProcessor& owner;
    std::function<void()> onClose;
    juce::Label heading, hint, status;
    juce::TextEditor editor;
    juce::TextButton paste, import, close;
};

DelayEditor::DelayEditor (DelayProcessor& p)
    : AudioProcessorEditor (&p),
      owner (p),
      mixA (p.apvts, "mix", mix),
      feedbackA (p.apvts, "feedback", feedback),
      panA (p.apvts, "pan", pan),
      stepsA (p.apvts, "steps", steps)
{
    setLookAndFeel (&lnf);
    setSize (kEditorW, kEditorH);
    setResizable (false, false);

    title.setText ("OpenRB Delay", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centredLeft);
    title.setColour (juce::Label::textColourId, kAmber);
    title.setFont (juce::FontOptions().withHeight (18.0f).withStyle ("Bold"));
    addAndMakeVisible (title);

    auto setupName = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, kMuted);
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };

    setupKnob (mix);
    setupKnob (feedback);
    setupKnob (pan);
    setupIncDec (steps);
    setupModeButtons (p);

    setupName (mixN, "MIX");
    setupName (feedbackN, "F.BACK");
    setupName (panN, "PAN");
    setupName (modeN, "MODE");
    setupName (stepsN, "STEPS");
    modeN.setJustificationType (juce::Justification::centredLeft);
    stepsN.setJustificationType (juce::Justification::centredLeft);

    gear.onClick = [this] { setImportOpen (importPanel == nullptr || ! importPanel->isVisible()); };
    addAndMakeVisible (gear);
}

DelayEditor::~DelayEditor()
{
    setLookAndFeel (nullptr);
}

void DelayEditor::setupKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    s.setNumDecimalPlacesToDisplay (0);
    s.setMouseDragSensitivity (140);
    addAndMakeVisible (s);
}

void DelayEditor::setupIncDec (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::IncDecButtons);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 28);
    s.setNumDecimalPlacesToDisplay (0);
    s.setColour (juce::Slider::textBoxBackgroundColourId, kWell);
    s.setColour (juce::Slider::textBoxOutlineColourId, kLine);
    addAndMakeVisible (s);
}

void DelayEditor::setupModeButtons (DelayProcessor& p)
{
    auto* modeParam = p.apvts.getParameter ("mode");
    jassert (modeParam != nullptr);

    for (auto* b : { &triplet, &sixteenth })
    {
        b->setRadioGroupId (kModeRadioGroup);
        b->setClickingTogglesState (true);
        b->setColour (juce::ToggleButton::textColourId, kText);
        addAndMakeVisible (b);
    }

    modeA = std::make_unique<juce::ParameterAttachment> (
        *modeParam,
        [this] (float value)
        {
            const juce::ScopedValueSetter<bool> svs (ignoreModeCallbacks, true);
            const int index = juce::roundToInt (value);
            triplet.setToggleState (index == 0, juce::dontSendNotification);
            sixteenth.setToggleState (index == 1, juce::dontSendNotification);
        });

    auto onClick = [this]
    {
        if (ignoreModeCallbacks || modeA == nullptr)
            return;
        modeA->setValueAsCompleteGesture (sixteenth.getToggleState() ? 1.0f : 0.0f);
    };

    triplet.onClick = onClick;
    sixteenth.onClick = onClick;
    modeA->sendInitialUpdate();
}

void DelayEditor::setImportOpen (bool open)
{
    if (open)
    {
        if (importPanel == nullptr)
        {
            importPanel = std::make_unique<RecallImportPanel> (owner, [this] { setImportOpen (false); });
            addAndMakeVisible (*importPanel);
        }
        importPanel->setBounds (getLocalBounds());
        importPanel->setVisible (true);
        importPanel->toFront (false);
        gear.toFront (false);
        importPanel->reset();
        return;
    }

    if (importPanel != nullptr)
        importPanel->setVisible (false);
}

void DelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    auto panel = getLocalBounds().toFloat().reduced (12.0f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (panel, 8.0f, 1.0f);
}

void DelayEditor::resized()
{
    auto bounds = getLocalBounds();
    if (importPanel != nullptr)
        importPanel->setBounds (bounds);

    auto top = bounds.reduced (16);
    gear.setBounds (top.removeFromTop (kGearSize).removeFromRight (kGearSize));
    gear.toFront (false);

    auto r = getLocalBounds().reduced (24);
    auto titleRow = r.removeFromTop (28);
    titleRow.removeFromRight (kGearSize);
    title.setBounds (titleRow);
    r.removeFromTop (8);

    auto right = r.removeFromRight (140);
    r.removeFromRight (8);

    const int n = 3;
    const int w = r.getWidth() / n;
    const int y = r.getY();
    const int h = r.getHeight();

    auto place = [&] (juce::Slider& s, juce::Label& name, int i)
    {
        auto b = juce::Rectangle<int> (r.getX() + i * w, y, w, h).reduced (6, 0);
        name.setBounds (b.removeFromTop (18));
        s.setBounds (b);
    };

    place (mix, mixN, 0);
    place (feedback, feedbackN, 1);
    place (pan, panN, 2);

    modeN.setBounds (right.removeFromTop (18));
    triplet.setBounds (right.removeFromTop (24));
    sixteenth.setBounds (right.removeFromTop (24));
    right.removeFromTop (16);
    stepsN.setBounds (right.removeFromTop (18));
    steps.setBounds (right.removeFromTop (48).removeFromLeft (96));
}
