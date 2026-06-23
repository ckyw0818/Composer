#include "ui/TransportBar.h"

#include "ui/Theme.h"

#include <cmath>

namespace composer::ui {
namespace {

juce::String formatPosition(const domain::ProjectSample sample, const double sampleRate,
    const double tempoBpm) {
    const double seconds = sampleRate > 0.0 ? static_cast<double>(sample) / sampleRate : 0.0;
    const double beatsPerSecond = tempoBpm / 60.0;
    const double totalBeats = seconds * beatsPerSecond;
    const int bar = static_cast<int>(totalBeats) / 4 + 1;
    const int beat = static_cast<int>(totalBeats) % 4 + 1;
    return juce::String::formatted("%d.%d", bar, beat);
}

}  // namespace

TransportBar::TransportBar(audio::PlaybackEngine& engine, const double tempoBpm)
    : engine_(engine), tempoBpm_(tempoBpm) {
    auto styleButton = [this](juce::TextButton& button) {
        button.setColour(juce::TextButton::buttonColourId, theme::raised);
        button.setColour(juce::TextButton::buttonOnColourId, theme::selection);
        button.setColour(juce::TextButton::textColourOffId, theme::textPrimary);
        button.setColour(juce::TextButton::textColourOnId, theme::canvas);
        addAndMakeVisible(button);
    };
    styleButton(rewindButton_);
    styleButton(playButton_);
    styleButton(recordButton_);
    styleButton(loopButton_);
    styleButton(metronomeButton_);

    loopButton_.setClickingTogglesState(true);
    loopButton_.setToggleState(engine_.isLooping(), juce::dontSendNotification);
    metronomeButton_.setClickingTogglesState(true);
    metronomeButton_.setToggleState(engine_.isMetronomeEnabled(), juce::dontSendNotification);

    rewindButton_.onClick = [this] {
        engine_.rewind();
        refresh();
    };
    playButton_.onClick = [this] {
        engine_.togglePlay();
        refresh();
    };
    recordButton_.onClick = [this] {
        if (onRecordClicked) onRecordClicked();
    };
    loopButton_.onClick = [this] {
        engine_.setLooping(loopButton_.getToggleState());
    };
    metronomeButton_.onClick = [this] {
        engine_.setMetronomeEnabled(metronomeButton_.getToggleState());
    };

    positionLabel_.setColour(juce::Label::textColourId, theme::textPrimary);
    positionLabel_.setJustificationType(juce::Justification::centredLeft);
    positionLabel_.setFont(juce::FontOptions{18.0F});
    addAndMakeVisible(positionLabel_);

    tempo_.setRange(20.0, 300.0, 1.0);
    tempo_.setSliderStyle(juce::Slider::LinearHorizontal);
    tempo_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    tempo_.setTextValueSuffix(" BPM");
    tempo_.setTitle("Project tempo");
    tempo_.setValue(tempoBpm_, juce::dontSendNotification);
    tempo_.onValueChange = [this] {
        const double value = tempo_.getValue();
        if (value != tempoBpm_ && onTempoChanged) onTempoChanged(value);
    };
    addAndMakeVisible(tempo_);

    signatureLabel_.setColour(juce::Label::textColourId, theme::textSecondary);
    signatureLabel_.setJustificationType(juce::Justification::centredRight);
    signatureLabel_.setText("4/4", juce::dontSendNotification);
    addAndMakeVisible(signatureLabel_);

    refresh();
    startTimerHz(30);
}

TransportBar::~TransportBar() {
    stopTimer();
}

void TransportBar::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
    graphics.setColour(theme::border);
    graphics.drawHorizontalLine(getHeight() - 1, 0.0F, static_cast<float>(getWidth()));
}

void TransportBar::resized() {
    auto area = getLocalBounds().reduced(12, 10);
    rewindButton_.setBounds(area.removeFromLeft(56));
    area.removeFromLeft(8);
    playButton_.setBounds(area.removeFromLeft(96));
    area.removeFromLeft(8);
    recordButton_.setBounds(area.removeFromLeft(96));
    area.removeFromLeft(8);
    loopButton_.setBounds(area.removeFromLeft(72));
    area.removeFromLeft(8);
    metronomeButton_.setBounds(area.removeFromLeft(72));
    area.removeFromLeft(16);
    signatureLabel_.setBounds(area.removeFromRight(44));
    area.removeFromRight(8);
    tempo_.setBounds(area.removeFromRight(210));
    positionLabel_.setBounds(area);
}

void TransportBar::setTempoBpm(const double beatsPerMinute) {
    tempoBpm_ = beatsPerMinute;
    tempo_.setValue(beatsPerMinute, juce::dontSendNotification);
    refresh();
}

void TransportBar::setRecording(const bool recording) {
    recordButton_.setToggleState(recording, juce::dontSendNotification);
    recordButton_.setButtonText(recording ? "Stop Rec" : "Record");
    recordButton_.setColour(juce::TextButton::buttonColourId,
        recording ? theme::record : theme::raised);
}

void TransportBar::timerCallback() {
    refresh();
}

void TransportBar::refresh() {
    playButton_.setButtonText(engine_.isPlaying() ? "Stop" : "Play");
    playButton_.setToggleState(engine_.isPlaying(), juce::dontSendNotification);
    positionLabel_.setText(
        formatPosition(engine_.playheadSamples(), engine_.sampleRate(), tempoBpm_),
        juce::dontSendNotification);
}

}  // namespace composer::ui
