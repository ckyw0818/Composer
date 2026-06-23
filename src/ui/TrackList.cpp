#include "ui/TrackList.h"

#include "ui/Theme.h"

#include <algorithm>
#include <array>

namespace composer::ui {

TrackList::TrackList(commands::ProjectEditor& editor) : editor_(editor) {
    nameLabel_.setText("Selected track", juce::dontSendNotification);
    nameLabel_.setColour(juce::Label::textColourId, theme::textSecondary);
    nameEditor_.setTitle("Selected track name");
    nameEditor_.setSelectAllWhenFocused(true);
    duplicateButton_.setTitle("Duplicate selected track");
    deleteButton_.setTitle("Delete selected track");
    for (auto* component : std::array<juce::Component*, 4>{
             &nameLabel_, &nameEditor_, &duplicateButton_, &deleteButton_}) {
        addAndMakeVisible(*component);
    }

    const auto commitRename = [this] {
        if (refreshingToolbar_) return;
        const auto id = selectedTrackId();
        const auto name = nameEditor_.getText().trim().toStdString();
        if (!id.empty() && !name.empty() && name != selectedTrackName() && onTrackRenamed) {
            onTrackRenamed(id, name);
        }
    };
    nameEditor_.onReturnKey = commitRename;
    nameEditor_.onFocusLost = commitRename;
    duplicateButton_.onClick = [this] {
        const auto id = selectedTrackId();
        if (!id.empty() && onTrackDuplicated) {
            onTrackDuplicated(id);
        }
    };
    deleteButton_.onClick = [this] {
        const auto id = selectedTrackId();
        if (!id.empty() && onTrackDeleteRequested) {
            onTrackDeleteRequested(id);
        }
    };
    refreshToolbar();
}

void TrackList::refreshFromProject() {
    const auto visibleRows = rows();
    const auto trackCount = static_cast<int>(visibleRows.size());
    if (selectedIndex_ >= trackCount) {
        selectedIndex_ = trackCount > 0 ? trackCount - 1 : 0;
    }
    const int contentHeight = trackCount * kRowHeight;
    const int visibleHeight = std::max(0, getHeight() - kToolbarHeight);
    scrollOffset_ = std::clamp(scrollOffset_, 0, std::max(0, contentHeight - visibleHeight));
    refreshToolbar();
    repaint();
}

void TrackList::selectTrack(const domain::EntityId& trackId) {
    const auto visibleRows = rows();
    for (int index = 0; index < static_cast<int>(visibleRows.size()); ++index) {
        if (visibleRows[static_cast<std::size_t>(index)].id == trackId) {
            selectedIndex_ = index;
            const int visibleHeight = std::max(0, getHeight() - kToolbarHeight);
            const int rowTop = index * kRowHeight;
            const int rowBottom = rowTop + kRowHeight;
            if (rowTop < scrollOffset_) scrollOffset_ = rowTop;
            if (rowBottom > scrollOffset_ + visibleHeight) {
                scrollOffset_ = std::max(0, rowBottom - visibleHeight);
            }
            refreshToolbar();
            repaint();
            return;
        }
    }
}

void TrackList::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
    graphics.setColour(theme::border);
    graphics.drawVerticalLine(getWidth() - 1, 0.0F, static_cast<float>(getHeight()));

    const auto visibleRows = rows();
    for (int index = 0; index < static_cast<int>(visibleRows.size()); ++index) {
        const auto rowInfo = visibleRows[static_cast<std::size_t>(index)];
        const auto* midiTrack = rowInfo.audio ? nullptr : &editor_.project().tracks[rowInfo.index];
        const auto* audioTrack = rowInfo.audio ? &editor_.project().audioTracks[rowInfo.index] : nullptr;
        const juce::Rectangle<int> row{
            0, kToolbarHeight + index * kRowHeight - scrollOffset_, getWidth(), kRowHeight};
        if (row.getBottom() <= kToolbarHeight || row.getY() >= getHeight()) continue;
        const bool selected = index == selectedIndex_;

        graphics.setColour(selected ? theme::raised : theme::panel);
        graphics.fillRect(row);
        if (selected) {
            graphics.setColour(theme::selection);
            graphics.fillRect(row.withWidth(3));
        }

        graphics.setColour(theme::textPrimary);
        graphics.setFont(juce::FontOptions{16.0F});
        graphics.drawText(rowInfo.audio ? audioTrack->name : midiTrack->name,
            row.reduced(12, 6).removeFromTop(22),
            juce::Justification::topLeft);

        const bool muted = rowInfo.audio ? audioTrack->muted : midiTrack->muted;
        graphics.setColour(muted ? theme::warning : theme::textSecondary);
        graphics.setFont(juce::FontOptions{13.0F});
        const juce::String subtitle = rowInfo.audio
            ? (juce::String{"Audio in "} + juce::String{audioTrack->input.channelIndex + 1}
                  + (audioTrack->recordArmed ? juce::String{"  ARM"} : juce::String{})
                  + (muted ? juce::String{"  (muted)"} : juce::String{}))
            : (juce::String{midiTrack->instrumentId}
                  + (muted ? juce::String{"  (muted)"} : juce::String{}));
        graphics.drawText(subtitle, row.reduced(12, 6).removeFromBottom(22),
            juce::Justification::bottomLeft);
        if (rowInfo.audio) {
            const auto armBounds = row.withX(row.getRight() - 42).withWidth(42).reduced(7, 14);
            graphics.setColour(audioTrack->recordArmed ? theme::record : theme::border);
            graphics.fillRoundedRectangle(armBounds.toFloat(), 3.0F);
            graphics.setColour(audioTrack->recordArmed ? theme::canvas : theme::textSecondary);
            graphics.drawText("R", armBounds, juce::Justification::centred);
        }

        graphics.setColour(theme::border);
        graphics.drawHorizontalLine(row.getBottom() - 1, 0.0F, static_cast<float>(getWidth()));
    }
    if (dragTargetIndex_ >= 0) {
        graphics.setColour(theme::selection);
        const int y = kToolbarHeight + dragTargetIndex_ * kRowHeight - scrollOffset_;
        graphics.fillRect(0, y, getWidth(), 3);
    }
}

void TrackList::resized() {
    auto toolbar = getLocalBounds().removeFromTop(kToolbarHeight).reduced(8, 5);
    nameLabel_.setBounds(toolbar.removeFromTop(18));
    auto controls = toolbar.removeFromTop(28);
    deleteButton_.setBounds(controls.removeFromRight(58));
    controls.removeFromRight(4);
    duplicateButton_.setBounds(controls.removeFromRight(72));
    controls.removeFromRight(5);
    nameEditor_.setBounds(controls);
    refreshFromProject();
}

void TrackList::mouseDown(const juce::MouseEvent& event) {
    const auto visibleRows = rows();
    if (event.y < kToolbarHeight) return;
    const int index = (event.y - kToolbarHeight + scrollOffset_) / kRowHeight;
    if (index < 0 || index >= static_cast<int>(visibleRows.size())) {
        return;
    }

    const auto rowInfo = visibleRows[static_cast<std::size_t>(index)];
    selectedIndex_ = index;
    dragSourceIndex_ = index;
    dragTargetIndex_ = index;
    refreshToolbar();
    if (rowInfo.audio && event.x > getWidth() - 50 && onTrackArmed) {
        const auto& audioTrack = editor_.project().audioTracks[rowInfo.index];
        onTrackArmed(audioTrack.id, !audioTrack.recordArmed);
        return;
    }
    if (onTrackSelected) {
        onTrackSelected(rowInfo.id);
    }
    repaint();
}

void TrackList::mouseDrag(const juce::MouseEvent& event) {
    const auto visibleRows = rows();
    if (dragSourceIndex_ < 0 || visibleRows.empty()) return;
    const int index = std::clamp((event.y - kToolbarHeight + scrollOffset_) / kRowHeight, 0,
        static_cast<int>(visibleRows.size()) - 1);
    if (dragTargetIndex_ != index) {
        dragTargetIndex_ = index;
        repaint();
    }
}

void TrackList::mouseUp(const juce::MouseEvent&) {
    if (dragSourceIndex_ >= 0 && dragTargetIndex_ >= 0
        && dragSourceIndex_ != dragTargetIndex_ && onTrackReordered) {
        const auto visibleRows = rows();
        if (dragSourceIndex_ < static_cast<int>(visibleRows.size())) {
            onTrackReordered(visibleRows[static_cast<std::size_t>(dragSourceIndex_)].id,
                static_cast<std::size_t>(dragTargetIndex_));
        }
    }
    dragSourceIndex_ = -1;
    dragTargetIndex_ = -1;
    repaint();
}

void TrackList::mouseWheelMove(
    const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const int contentHeight = static_cast<int>(rows().size()) * kRowHeight;
    const int visibleHeight = std::max(0, getHeight() - kToolbarHeight);
    const int maximum = std::max(0, contentHeight - visibleHeight);
    scrollOffset_ = std::clamp(
        scrollOffset_ - static_cast<int>(wheel.deltaY * 180.0F), 0, maximum);
    repaint();
}

const domain::InstrumentTrack* TrackList::selectedTrack() const {
    const auto visibleRows = rows();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(visibleRows.size())) return nullptr;
    const auto row = visibleRows[static_cast<std::size_t>(selectedIndex_)];
    return row.audio ? nullptr : &editor_.project().tracks[row.index];
}

const domain::AudioTrack* TrackList::selectedAudioTrack() const {
    const auto visibleRows = rows();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(visibleRows.size())) return nullptr;
    const auto row = visibleRows[static_cast<std::size_t>(selectedIndex_)];
    return row.audio ? &editor_.project().audioTracks[row.index] : nullptr;
}

std::vector<TrackList::TrackRow> TrackList::rows() const {
    std::vector<TrackRow> result;
    const auto& project = editor_.project();
    result.reserve(project.tracks.size() + project.audioTracks.size());
    auto appendById = [&](const domain::EntityId& id) {
        for (std::size_t index = 0; index < project.tracks.size(); ++index) {
            if (project.tracks[index].id == id) {
                result.push_back({id, false, index});
                return;
            }
        }
        for (std::size_t index = 0; index < project.audioTracks.size(); ++index) {
            if (project.audioTracks[index].id == id) {
                result.push_back({id, true, index});
                return;
            }
        }
    };
    if (!project.trackOrder.empty()) {
        for (const auto& id : project.trackOrder) appendById(id);
    }
    if (result.empty()) {
        for (std::size_t index = 0; index < project.tracks.size(); ++index) {
            result.push_back({project.tracks[index].id, false, index});
        }
        for (std::size_t index = 0; index < project.audioTracks.size(); ++index) {
            result.push_back({project.audioTracks[index].id, true, index});
        }
    }
    return result;
}

domain::EntityId TrackList::selectedTrackId() const {
    if (const auto* track = selectedTrack(); track != nullptr) return track->id;
    if (const auto* track = selectedAudioTrack(); track != nullptr) return track->id;
    return {};
}

std::string TrackList::selectedTrackName() const {
    if (const auto* track = selectedTrack(); track != nullptr) return track->name;
    if (const auto* track = selectedAudioTrack(); track != nullptr) return track->name;
    return {};
}

void TrackList::refreshToolbar() {
    refreshingToolbar_ = true;
    const auto id = selectedTrackId();
    const bool enabled = !id.empty();
    nameEditor_.setEnabled(enabled);
    duplicateButton_.setEnabled(enabled);
    deleteButton_.setEnabled(enabled);
    nameEditor_.setText(enabled ? juce::String{selectedTrackName()} : juce::String{}, false);
    refreshingToolbar_ = false;
}

}  // namespace composer::ui
