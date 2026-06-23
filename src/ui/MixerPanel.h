#pragma once

#include "commands/ProjectEditor.h"
#include "domain/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace composer::ui {

class MixerPanel final : public juce::Component {
public:
    explicit MixerPanel(commands::ProjectEditor& editor);

    void setTrack(const domain::EntityId& trackId);
    void refreshFromProject();
    std::function<void()> onProjectChanged;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    [[nodiscard]] const domain::InstrumentTrack* currentTrack() const;
    void notifyChanged();

    commands::ProjectEditor& editor_;
    domain::EntityId trackId_;
    juce::Label title_;
    juce::Label volumeLabel_;
    juce::Slider volume_;
    juce::Label panLabel_;
    juce::Slider pan_;
    juce::ToggleButton mute_{"Mute"};
    juce::ToggleButton solo_{"Solo"};
    bool refreshing_{false};
};

}  // namespace composer::ui
