#include "ui/MixerPanel.h"

#include "ui/Theme.h"

#include <array>
#include <variant>

namespace composer::ui {

MixerPanel::MixerPanel(commands::ProjectEditor& editor) : editor_(editor) {
    title_.setText("Mixer", juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, theme::textPrimary);
    title_.setFont(juce::FontOptions{17.0F});
    volumeLabel_.setText("Volume", juce::dontSendNotification);
    panLabel_.setText("Pan", juce::dontSendNotification);
    for (auto* label : {&volumeLabel_, &panLabel_}) {
        label->setColour(juce::Label::textColourId, theme::textSecondary);
    }

    volume_.setRange(0.0, 2.0, 0.01);
    volume_.setSliderStyle(juce::Slider::LinearHorizontal);
    volume_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
    volume_.setTitle("Track volume");
    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setSliderStyle(juce::Slider::LinearHorizontal);
    pan_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
    pan_.setTitle("Track pan");

    for (auto* component : std::array<juce::Component*, 7>{
             &title_, &volumeLabel_, &volume_, &panLabel_, &pan_, &mute_, &solo_}) {
        addAndMakeVisible(*component);
    }

    volume_.onValueChange = [this] {
        if (!refreshing_ && std::holds_alternative<std::monostate>(
                editor_.setTrackVolume(trackId_, static_cast<float>(volume_.getValue())))) {
            notifyChanged();
        }
    };
    pan_.onValueChange = [this] {
        if (!refreshing_ && std::holds_alternative<std::monostate>(
                editor_.setTrackPan(trackId_, static_cast<float>(pan_.getValue())))) {
            notifyChanged();
        }
    };
    mute_.onClick = [this] {
        if (!refreshing_ && std::holds_alternative<std::monostate>(
                editor_.setTrackMuted(trackId_, mute_.getToggleState()))) {
            notifyChanged();
        }
    };
    solo_.onClick = [this] {
        if (!refreshing_ && std::holds_alternative<std::monostate>(
                editor_.setTrackSoloed(trackId_, solo_.getToggleState()))) {
            notifyChanged();
        }
    };
}

const domain::InstrumentTrack* MixerPanel::currentTrack() const {
    for (const auto& track : editor_.project().tracks) {
        if (track.id == trackId_) return &track;
    }
    return nullptr;
}

void MixerPanel::setTrack(const domain::EntityId& trackId) {
    trackId_ = trackId;
    refreshFromProject();
}

void MixerPanel::refreshFromProject() {
    refreshing_ = true;
    const auto* track = currentTrack();
    const bool enabled = track != nullptr;
    for (auto* component : std::array<juce::Component*, 4>{&volume_, &pan_, &mute_, &solo_}) {
        component->setEnabled(enabled);
    }
    if (track != nullptr) {
        title_.setText("Mixer - " + juce::String{track->name}, juce::dontSendNotification);
        volume_.setValue(track->volume, juce::dontSendNotification);
        pan_.setValue(track->pan, juce::dontSendNotification);
        mute_.setToggleState(track->muted, juce::dontSendNotification);
        solo_.setToggleState(track->soloed, juce::dontSendNotification);
    } else {
        title_.setText("Mixer - no track selected", juce::dontSendNotification);
    }
    refreshing_ = false;
    repaint();
}

void MixerPanel::notifyChanged() {
    refreshFromProject();
    if (onProjectChanged) onProjectChanged();
}

void MixerPanel::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
    graphics.setColour(theme::border);
    graphics.drawHorizontalLine(0, 0.0F, static_cast<float>(getWidth()));
}

void MixerPanel::resized() {
    auto area = getLocalBounds().reduced(12, 8);
    title_.setBounds(area.removeFromLeft(180));
    volumeLabel_.setBounds(area.removeFromLeft(54));
    volume_.setBounds(area.removeFromLeft(220));
    area.removeFromLeft(12);
    panLabel_.setBounds(area.removeFromLeft(32));
    pan_.setBounds(area.removeFromLeft(220));
    area.removeFromLeft(16);
    mute_.setBounds(area.removeFromLeft(72));
    solo_.setBounds(area.removeFromLeft(72));
}

}  // namespace composer::ui
