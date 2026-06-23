#pragma once

#include "commands/ProjectEditor.h"
#include "domain/Project.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace composer::ui {

// Left-hand track headers show name, instrument and mute state and select a track for the piano roll
// and mixer. Selecting a row fires onTrackSelected.
class TrackList final : public juce::Component {
public:
    explicit TrackList(commands::ProjectEditor& editor);

    void refreshFromProject();
    void selectTrack(const domain::EntityId& trackId);
    [[nodiscard]] int selectedIndex() const noexcept { return selectedIndex_; }

    std::function<void(const domain::EntityId& trackId)> onTrackSelected;
    std::function<void(const domain::EntityId& trackId, const std::string& name)> onTrackRenamed;
    std::function<void(const domain::EntityId& trackId)> onTrackDuplicated;
    std::function<void(const domain::EntityId& trackId)> onTrackDeleteRequested;
    std::function<void(const domain::EntityId& trackId, std::size_t newIndex)> onTrackReordered;
    std::function<void(const domain::EntityId& trackId, bool armed)> onTrackArmed;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(
        const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    static constexpr int kRowHeight = 56;
    static constexpr int kToolbarHeight = 78;

    void refreshToolbar();
    struct TrackRow final {
        domain::EntityId id;
        bool audio{};
        std::size_t index{};
    };
    [[nodiscard]] std::vector<TrackRow> rows() const;
    [[nodiscard]] domain::EntityId selectedTrackId() const;
    [[nodiscard]] std::string selectedTrackName() const;
    [[nodiscard]] const domain::InstrumentTrack* selectedTrack() const;
    [[nodiscard]] const domain::AudioTrack* selectedAudioTrack() const;

    commands::ProjectEditor& editor_;
    juce::Label nameLabel_;
    juce::TextEditor nameEditor_;
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton deleteButton_{"Delete"};
    int selectedIndex_{0};
    int dragSourceIndex_{-1};
    int dragTargetIndex_{-1};
    int scrollOffset_{0};
    bool refreshingToolbar_{false};
};

}  // namespace composer::ui
