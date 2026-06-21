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
    styleButton(loopButton_);

    loopButton_.setClickingTogglesState(true);
    loopButton_.setToggleState(engine_.isLooping(), juce::dontSendNotification);

    rewindButton_.onClick = [this] {
        engine_.rewind();
        refresh();
    };
    playButton_.onClick = [this] {
        engine_.togglePlay();
        refresh();
    };
    loopButton_.onClick = [this] {
        engine_.setLooping(loopButton_.getToggleState());
    };

    positionLabel_.setColour(juce::Label::textColourId, theme::textPrimary);
    positionLabel_.setJustificationType(juce::Justification::centredLeft);
    positionLabel_.setFont(juce::FontOptions{18.0F});
    addAndMakeVisible(positionLabel_);

    tempoLabel_.setColour(juce::Label::textColourId, theme::textSecondary);
    tempoLabel_.setJustificationType(juce::Justification::centredRight);
    tempoLabel_.setFont(juce::FontOptions{16.0F});
    tempoLabel_.setText(juce::String::formatted("%d BPM  4/4", static_cast<int>(tempoBpm_)),
        juce::dontSendNotification);
    addAndMakeVisible(tempoLabel_);

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
    loopButton_.setBounds(area.removeFromLeft(72));
    area.removeFromLeft(16);
    tempoLabel_.setBounds(area.removeFromRight(160));
    positionLabel_.setBounds(area);
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
