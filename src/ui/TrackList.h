#pragma once

#include "commands/ProjectEditor.h"
#include "domain/Project.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace composer::ui {

// Left-hand track headers. S1 shows name, instrument and a mute toggle, and lets the user select a
// track to load its first clip into the piano roll. Selecting a row fires onTrackSelected.
class TrackList final : public juce::Component {
public:
    explicit TrackList(commands::ProjectEditor& editor);

    void refreshFromProject();
    [[nodiscard]] int selectedIndex() const noexcept { return selectedIndex_; }

    std::function<void(const domain::EntityId& trackId)> onTrackSelected;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    static constexpr int kRowHeight = 56;

    commands::ProjectEditor& editor_;
    int selectedIndex_{0};
};

}  // namespace composer::ui
