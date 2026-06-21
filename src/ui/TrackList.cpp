#include "ui/TrackList.h"

#include "ui/Theme.h"

namespace composer::ui {

TrackList::TrackList(commands::ProjectEditor& editor) : editor_(editor) {}

void TrackList::refreshFromProject() {
    const auto trackCount = static_cast<int>(editor_.project().tracks.size());
    if (selectedIndex_ >= trackCount) {
        selectedIndex_ = trackCount > 0 ? trackCount - 1 : 0;
    }
    repaint();
}

void TrackList::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
    graphics.setColour(theme::border);
    graphics.drawVerticalLine(getWidth() - 1, 0.0F, static_cast<float>(getHeight()));

    const auto& tracks = editor_.project().tracks;
    for (int index = 0; index < static_cast<int>(tracks.size()); ++index) {
        const auto& track = tracks[static_cast<std::size_t>(index)];
        const juce::Rectangle<int> row{0, index * kRowHeight, getWidth(), kRowHeight};
        const bool selected = index == selectedIndex_;

        graphics.setColour(selected ? theme::raised : theme::panel);
        graphics.fillRect(row);
        if (selected) {
            graphics.setColour(theme::selection);
            graphics.fillRect(row.withWidth(3));
        }

        graphics.setColour(theme::textPrimary);
        graphics.setFont(juce::FontOptions{16.0F});
        graphics.drawText(track.name, row.reduced(12, 6).removeFromTop(22),
            juce::Justification::topLeft);

        graphics.setColour(track.muted ? theme::warning : theme::textSecondary);
        graphics.setFont(juce::FontOptions{13.0F});
        const juce::String subtitle = juce::String{track.instrumentId}
            + (track.muted ? juce::String{"  (muted)"} : juce::String{});
        graphics.drawText(subtitle, row.reduced(12, 6).removeFromBottom(22),
            juce::Justification::bottomLeft);

        graphics.setColour(theme::border);
        graphics.drawHorizontalLine(row.getBottom() - 1, 0.0F, static_cast<float>(getWidth()));
    }
}

void TrackList::resized() {}

void TrackList::mouseDown(const juce::MouseEvent& event) {
    const auto& tracks = editor_.project().tracks;
    const int index = event.y / kRowHeight;
    if (index < 0 || index >= static_cast<int>(tracks.size())) {
        return;
    }

    const auto& track = tracks[static_cast<std::size_t>(index)];
    selectedIndex_ = index;
    if (onTrackSelected) {
        onTrackSelected(track.id);
    }
    repaint();
}

}  // namespace composer::ui
