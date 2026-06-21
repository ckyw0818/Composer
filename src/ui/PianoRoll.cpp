#include "ui/PianoRoll.h"

#include "ui/Theme.h"

#include <algorithm>
#include <variant>

namespace composer::ui {
namespace {

constexpr domain::Tick kBeatTicks = domain::kPpq;
constexpr int kResizeHandlePx = 6;

[[nodiscard]] bool isBlackKey(const domain::Pitch pitch) {
    switch (pitch % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

template <typename T>
[[nodiscard]] bool ok(const application::Result<T>& result) {
    return std::holds_alternative<T>(result);
}

}  // namespace

PianoRoll::PianoRoll(commands::ProjectEditor& editor, const double tempoBpm)
    : editor_(editor), tempoBpm_(tempoBpm) {
    setWantsKeyboardFocus(true);
}

void PianoRoll::showClip(const domain::EntityId& trackId, const domain::EntityId& clipId) {
    trackId_ = trackId;
    clipId_ = clipId;
    selectedNote_ = {};
    refreshFromProject();
}

void PianoRoll::refreshFromProject() {
    repaint();
}

const domain::MidiClip* PianoRoll::currentClip() const {
    for (const auto& track : editor_.project().tracks) {
        if (!(track.id == trackId_)) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (clip.id == clipId_) {
                return &clip;
            }
        }
    }
    return nullptr;
}

int PianoRoll::rowForPitch(const domain::Pitch pitch) const {
    return kHighPitch - pitch;  // row 0 is the highest pitch at the top
}

domain::Pitch PianoRoll::pitchForY(const int y) const {
    const int row = std::clamp(y / rowHeight_, 0, kPitchCount - 1);
    return static_cast<domain::Pitch>(kHighPitch - row);
}

domain::Tick PianoRoll::tickForX(const int x) const {
    const double beats = static_cast<double>(x) / pixelsPerBeat_;
    return static_cast<domain::Tick>(beats * static_cast<double>(kBeatTicks));
}

int PianoRoll::xForTick(const domain::Tick tick) const {
    const double beats = static_cast<double>(tick) / static_cast<double>(kBeatTicks);
    return static_cast<int>(beats * pixelsPerBeat_);
}

int PianoRoll::yForPitch(const domain::Pitch pitch) const {
    return rowForPitch(pitch) * rowHeight_;
}

domain::Tick PianoRoll::snap(const domain::Tick tick, const bool suspend) const {
    if (suspend) {
        return std::max<domain::Tick>(0, tick);
    }
    const domain::Tick grid = kBeatTicks;  // snap to the beat in S1
    const domain::Tick snapped = (tick + grid / 2) / grid * grid;
    return std::max<domain::Tick>(0, snapped);
}

juce::Rectangle<int> PianoRoll::boundsForNote(const domain::NoteEvent& note) const {
    const int x = xForTick(note.start);
    const int width = std::max(4, xForTick(note.end()) - x);
    const int y = yForPitch(note.pitch);
    return juce::Rectangle<int>{x, y, width, rowHeight_};
}

std::optional<domain::EntityId> PianoRoll::noteAt(const juce::Point<int> point) const {
    const auto* clip = currentClip();
    if (clip == nullptr) {
        return std::nullopt;
    }
    // Iterate in reverse so the topmost (last drawn) note wins on overlap.
    for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it) {
        if (boundsForNote(*it).contains(point)) {
            return it->id;
        }
    }
    return std::nullopt;
}

void PianoRoll::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::canvas);

    const auto* clip = currentClip();
    if (clip == nullptr) {
        graphics.setColour(theme::textSecondary);
        graphics.setFont(juce::FontOptions{16.0F});
        graphics.drawText("Select a MIDI clip to edit notes.", getLocalBounds(),
            juce::Justification::centred);
        return;
    }

    // Pitch rows: shade black-key rows for readability.
    for (int row = 0; row < kPitchCount; ++row) {
        const auto pitch = static_cast<domain::Pitch>(kHighPitch - row);
        const int y = row * rowHeight_;
        graphics.setColour(isBlackKey(pitch) ? theme::panel : theme::raised.withAlpha(0.25F));
        graphics.fillRect(0, y, getWidth(), rowHeight_);
    }

    // Beat / bar grid lines across the clip.
    graphics.setColour(theme::border);
    const int beats = static_cast<int>(clip->range.length() / kBeatTicks);
    for (int beat = 0; beat <= beats; ++beat) {
        const int x = xForTick(static_cast<domain::Tick>(beat) * kBeatTicks);
        const bool barLine = beat % 4 == 0;
        graphics.setColour(barLine ? theme::border : theme::border.withAlpha(0.4F));
        graphics.drawVerticalLine(x, 0.0F, static_cast<float>(getHeight()));
    }

    // Notes.
    for (const auto& note : clip->notes) {
        const auto bounds = boundsForNote(note);
        const bool selected = note.id == selectedNote_;
        graphics.setColour(selected ? theme::selection : theme::success);
        graphics.fillRect(bounds.reduced(0, 1));
        graphics.setColour(theme::canvas);
        graphics.drawRect(bounds.reduced(0, 1), 1);
    }
}

void PianoRoll::resized() {
    rowHeight_ = std::max(8, getHeight() / kPitchCount);
}

void PianoRoll::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    const auto* clip = currentClip();
    if (clip == nullptr) {
        return;
    }

    const auto hit = noteAt(event.getPosition());
    if (!hit.has_value()) {
        selectedNote_ = {};
        dragMode_ = DragMode::none;
        repaint();
        return;
    }

    selectedNote_ = *hit;
    // Capture original geometry for the drag.
    for (const auto& note : clip->notes) {
        if (note.id == selectedNote_) {
            dragOriginalStart_ = note.start;
            dragOriginalPitch_ = note.pitch;
            dragOriginalDuration_ = note.duration;
            const auto bounds = boundsForNote(note);
            const bool onRightEdge = event.x >= bounds.getRight() - kResizeHandlePx;
            dragMode_ = onRightEdge ? DragMode::resize : DragMode::move;
            break;
        }
    }
    dragStartTick_ = tickForX(event.x);
    dragStartPitch_ = pitchForY(event.y);
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event) {
    if (dragMode_ == DragMode::none || selectedNote_.empty()) {
        return;
    }
    const bool suspendSnap = event.mods.isAltDown();

    if (dragMode_ == DragMode::move) {
        const domain::Tick deltaTick = tickForX(event.x) - dragStartTick_;
        const int deltaPitch = static_cast<int>(pitchForY(event.y))
            - static_cast<int>(dragStartPitch_);
        const domain::Tick newStart = snap(dragOriginalStart_ + deltaTick, suspendSnap);
        const auto newPitch = static_cast<domain::Pitch>(
            std::clamp(static_cast<int>(dragOriginalPitch_) + deltaPitch, kLowPitch, kHighPitch));
        const auto result = editor_.moveNote(trackId_, clipId_, selectedNote_, newStart, newPitch);
        if (ok(result)) {
            if (onProjectChanged) {
                onProjectChanged();
            }
            repaint();
        }
    } else if (dragMode_ == DragMode::resize) {
        const domain::Tick pointerTick = snap(tickForX(event.x), suspendSnap);
        const domain::Tick newDuration = std::max<domain::Tick>(
            kBeatTicks / 4, pointerTick - dragOriginalStart_);
        const auto result = editor_.resizeNote(trackId_, clipId_, selectedNote_, newDuration);
        if (ok(result)) {
            if (onProjectChanged) {
                onProjectChanged();
            }
            repaint();
        }
    }
}

void PianoRoll::mouseUp(const juce::MouseEvent&) {
    dragMode_ = DragMode::none;
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& event) {
    const auto* clip = currentClip();
    if (clip == nullptr) {
        return;
    }
    if (noteAt(event.getPosition()).has_value()) {
        return;  // double-click on a note does not add another
    }

    const domain::Tick start = snap(tickForX(event.x), event.mods.isAltDown());
    const domain::Pitch pitch = pitchForY(event.y);
    const domain::Tick duration = kBeatTicks;
    const auto result = editor_.addNote(trackId_, clipId_, start, duration, pitch, 100);
    if (ok(result)) {
        selectedNote_ = std::get<domain::EntityId>(result);
        if (onProjectChanged) {
            onProjectChanged();
        }
        repaint();
    }
}

bool PianoRoll::keyPressed(const juce::KeyPress& key) {
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && !selectedNote_.empty()) {
        const auto result = editor_.deleteNote(trackId_, clipId_, selectedNote_);
        if (ok(result)) {
            selectedNote_ = {};
            if (onProjectChanged) {
                onProjectChanged();
            }
            repaint();
        }
        return true;
    }
    // Ctrl+Z / Ctrl+Y undo-redo within the roll.
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        editor_.undo();
        selectedNote_ = {};
        if (onProjectChanged) {
            onProjectChanged();
        }
        repaint();
        return true;
    }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0)) {
        editor_.redo();
        selectedNote_ = {};
        if (onProjectChanged) {
            onProjectChanged();
        }
        repaint();
        return true;
    }
    return false;
}

}  // namespace composer::ui
