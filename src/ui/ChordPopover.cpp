#include "ui/ChordPopover.h"

#include "ui/Theme.h"

#include <algorithm>
#include <array>

namespace composer::ui {

ChordPopover::ChordPopover(const int defaultOctave, const domain::Tick defaultDuration) {
    setSize(500, 230);
    title_.setText("Insert chord at the piano-roll cursor", juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, theme::textPrimary);
    title_.setFont(juce::FontOptions{17.0F});

    constexpr std::array roots{"C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"};
    for (int index = 0; index < static_cast<int>(roots.size()); ++index) {
        root_.addItem(roots[static_cast<std::size_t>(index)], index + 1);
    }
    root_.setSelectedId(1, juce::dontSendNotification);
    root_.setTitle("Chord root");

    quality_.addItem("Major", 1);
    quality_.addItem("Minor", 2);
    quality_.addItem("Diminished", 3);
    quality_.addItem("Augmented", 4);
    quality_.addItem("Dominant", 5);
    quality_.setSelectedId(1, juce::dontSendNotification);
    quality_.setTitle("Chord quality");

    extension_.addItem("Triad", 1);
    extension_.addItem("Seventh", 2);
    extension_.addItem("Ninth", 3);
    extension_.setSelectedId(1, juce::dontSendNotification);
    extension_.setTitle("Chord extension");

    for (int inversion = 0; inversion <= 4; ++inversion) {
        inversion_.addItem("Inv " + juce::String{inversion}, inversion + 1);
    }
    inversion_.setSelectedId(1, juce::dontSendNotification);
    inversion_.setTitle("Chord inversion");

    for (int octave = 1; octave <= 7; ++octave) {
        octave_.addItem("Oct " + juce::String{octave}, octave + 1);
    }
    octave_.setSelectedId(std::clamp(defaultOctave, 1, 7) + 1,
        juce::dontSendNotification);
    octave_.setTitle("Root octave");

    length_.addItem("1/16", 1);
    length_.addItem("1/8", 2);
    length_.addItem("1/4", 3);
    length_.addItem("1/2", 4);
    length_.addItem("Whole", 5);
    int lengthId = 3;
    if (defaultDuration <= domain::kPpq / 4) lengthId = 1;
    else if (defaultDuration <= domain::kPpq / 2) lengthId = 2;
    else if (defaultDuration >= domain::kPpq * 4) lengthId = 5;
    else if (defaultDuration >= domain::kPpq * 2) lengthId = 4;
    length_.setSelectedId(lengthId, juce::dontSendNotification);
    length_.setTitle("Inserted note length");

    velocity_.setRange(1.0, 127.0, 1.0);
    velocity_.setValue(100.0, juce::dontSendNotification);
    velocity_.setSliderStyle(juce::Slider::LinearHorizontal);
    velocity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
    velocity_.setTitle("Chord velocity");

    for (auto* component : std::array<juce::Component*, 12>{&title_, &root_, &quality_,
             &extension_, &inversion_, &octave_, &length_, &velocity_, &preview_, &insert_,
             &cancel_, &status_}) {
        addAndMakeVisible(*component);
    }
    status_.setColour(juce::Label::textColourId, theme::textSecondary);
    status_.setText("Preview is temporary. Insert creates editable MIDI notes.",
        juce::dontSendNotification);
    status_.setJustificationType(juce::Justification::centredLeft);

    preview_.onClick = [this] { if (onPreview) onPreview(request()); };
    insert_.onClick = [this] {
        if (onInsert && onInsert(request())) dismiss();
    };
    cancel_.onClick = [this] {
        if (onCancel) onCancel();
        dismiss();
    };
}

ChordRequest ChordPopover::request() const {
    ChordRequest result;
    result.spec.rootPitchClass = root_.getSelectedId() - 1;
    switch (quality_.getSelectedId()) {
        case 2: result.spec.quality = domain::ChordQuality::minor; break;
        case 3: result.spec.quality = domain::ChordQuality::diminished; break;
        case 4: result.spec.quality = domain::ChordQuality::augmented; break;
        case 5: result.spec.quality = domain::ChordQuality::dominant; break;
        default: result.spec.quality = domain::ChordQuality::major; break;
    }
    switch (extension_.getSelectedId()) {
        case 2: result.spec.extension = domain::ChordExtension::seventh; break;
        case 3: result.spec.extension = domain::ChordExtension::ninth; break;
        default: result.spec.extension = domain::ChordExtension::none; break;
    }
    result.spec.inversion = inversion_.getSelectedId() - 1;
    result.spec.octave = octave_.getSelectedId() - 1;
    result.spec.velocity = static_cast<domain::Velocity>(velocity_.getValue());
    switch (length_.getSelectedId()) {
        case 1: result.duration = domain::kPpq / 4; break;
        case 2: result.duration = domain::kPpq / 2; break;
        case 4: result.duration = domain::kPpq * 2; break;
        case 5: result.duration = domain::kPpq * 4; break;
        default: result.duration = domain::kPpq; break;
    }
    return result;
}

void ChordPopover::setStatus(const juce::String& text, const bool isError) {
    status_.setColour(juce::Label::textColourId,
        isError ? theme::warning : theme::textSecondary);
    status_.setText(text, juce::dontSendNotification);
}

void ChordPopover::dismiss() {
    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) {
        callout->dismiss();
    }
}

void ChordPopover::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
}

void ChordPopover::resized() {
    auto area = getLocalBounds().reduced(12, 8);
    title_.setBounds(area.removeFromTop(28));
    auto first = area.removeFromTop(32);
    root_.setBounds(first.removeFromLeft(62));
    first.removeFromLeft(4);
    quality_.setBounds(first.removeFromLeft(100));
    first.removeFromLeft(4);
    extension_.setBounds(first.removeFromLeft(88));
    first.removeFromLeft(4);
    inversion_.setBounds(first.removeFromLeft(74));
    first.removeFromLeft(4);
    octave_.setBounds(first.removeFromLeft(70));
    first.removeFromLeft(4);
    length_.setBounds(first);

    area.removeFromTop(8);
    auto second = area.removeFromTop(32);
    velocity_.setBounds(second.removeFromLeft(220));
    second.removeFromLeft(10);
    preview_.setBounds(second.removeFromLeft(72));
    insert_.setBounds(second.removeFromLeft(98));
    cancel_.setBounds(second.removeFromLeft(66));
    area.removeFromTop(8);
    status_.setBounds(area.removeFromTop(32));
}

}  // namespace composer::ui
