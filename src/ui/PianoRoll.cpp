#include "ui/PianoRoll.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace composer::ui {
namespace {

constexpr domain::Tick kBeatTicks = domain::kPpq;
constexpr int kResizeHandlePx = 6;
constexpr double kWheelScrollPixels = 240.0;
constexpr double kWheelZoomBase = 1.25;

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
    selectedNoteIds_.clear();
    rhythmPreview_.clear();
    if (const auto* clip = currentClip(); clip != nullptr) {
        viewport_.setRange(clip->range, true);
        insertionTick_ = clip->range.start;
    } else {
        viewport_.setRange({}, true);
        insertionTick_ = 0;
    }
    refreshFromProject();
}

void PianoRoll::refreshFromProject() {
    const auto* clip = currentClip();
    viewport_.setRange(clip != nullptr ? clip->range : domain::HalfOpenTickRange{}, false);
    std::erase_if(selectedNoteIds_, [&](const domain::EntityId& selectedId) {
        return clip == nullptr || std::none_of(clip->notes.begin(), clip->notes.end(),
            [&](const domain::NoteEvent& note) { return note.id == selectedId; });
    });
    repaint();
}

void PianoRoll::setGridTicks(const domain::Tick gridTicks) {
    if (gridTicks > 0) {
        gridTicks_ = gridTicks;
        insertionTick_ = viewport_.snapForInsertion(insertionTick_, gridTicks_, false);
        repaint();
    }
}

juce::Rectangle<int> PianoRoll::chordAnchorBoundsInParent() const {
    const auto bounds = getBoundsInParent();
    const int localX = std::clamp(xForTick(insertionTick_), 8, std::max(8, getWidth() - 8));
    return {bounds.getX() + localX, bounds.getY() + std::min(40, getHeight()), 2, 2};
}

std::vector<domain::EntityId> PianoRoll::rhythmSourceNoteIds() const {
    return selectedNoteIds_;
}

void PianoRoll::showRhythmPreview(std::vector<domain::GeneratedNote> notes) {
    rhythmPreview_ = std::move(notes);
    repaint();
}

void PianoRoll::clearRhythmPreview() {
    rhythmPreview_.clear();
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
    return viewport_.tickForX(x);
}

int PianoRoll::xForTick(const domain::Tick tick) const {
    return viewport_.xForTick(tick);
}

int PianoRoll::yForPitch(const domain::Pitch pitch) const {
    return rowForPitch(pitch) * rowHeight_;
}

domain::Tick PianoRoll::snap(const domain::Tick tick, const bool suspend) const {
    return suspend ? viewport_.clampToRange(tick)
                   : viewport_.snapNearest(tick, gridTicks_);
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

bool PianoRoll::isSelected(const domain::EntityId& noteId) const {
    return std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId)
        != selectedNoteIds_.end();
}

juce::Rectangle<int> PianoRoll::marqueeBounds() const {
    return juce::Rectangle<int>::leftTopRightBottom(
        std::min(marqueeAnchor_.x, marqueeCurrent_.x),
        std::min(marqueeAnchor_.y, marqueeCurrent_.y),
        std::max(marqueeAnchor_.x, marqueeCurrent_.x),
        std::max(marqueeAnchor_.y, marqueeCurrent_.y));
}

void PianoRoll::updateMarqueeSelection() {
    const auto* clip = currentClip();
    if (clip == nullptr) return;

    selectedNoteIds_ = marqueeBaseSelection_;
    const auto selection = marqueeBounds();
    for (const auto& note : clip->notes) {
        if (!selection.intersects(boundsForNote(note))) continue;
        const auto position = std::find(
            selectedNoteIds_.begin(), selectedNoteIds_.end(), note.id);
        if (marqueeToggle_ && position != selectedNoteIds_.end()) {
            selectedNoteIds_.erase(position);
        } else if (position == selectedNoteIds_.end()) {
            selectedNoteIds_.push_back(note.id);
        }
    }
}

void PianoRoll::notifyProjectChanged() {
    if (onProjectChanged) onProjectChanged();
    repaint();
}

void PianoRoll::copySelection() {
    const auto* clip = currentClip();
    if (clip == nullptr || selectedNoteIds_.empty()) return;

    domain::Tick firstStart = clip->range.end;
    for (const auto& note : clip->notes) {
        if (isSelected(note.id)) firstStart = std::min(firstStart, note.start);
    }
    clipboardNotes_.clear();
    for (const auto& note : clip->notes) {
        if (!isSelected(note.id)) continue;
        clipboardNotes_.push_back({
            note.start - firstStart, note.duration, note.pitch, note.velocity});
    }
}

void PianoRoll::pasteClipboard() {
    if (clipboardNotes_.empty() || currentClip() == nullptr) return;
    const domain::Tick pasteStart = viewport_.snapForInsertion(
        insertionTick_, gridTicks_, false);
    const auto result = editor_.pasteNotes(trackId_, clipId_, pasteStart, clipboardNotes_);
    if (!ok(result)) return;

    selectedNoteIds_ = std::get<std::vector<domain::EntityId>>(result);
    domain::Tick copiedSpan = 0;
    for (const auto& note : clipboardNotes_) copiedSpan = std::max(copiedSpan, note.end());
    insertionTick_ = pasteStart + copiedSpan;
    notifyProjectChanged();
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

    // Selected grid subdivision with stronger beat/bar lines.
    graphics.setColour(theme::border);
    const auto navigationRange = viewport_.range();
    const auto visibleStart = std::max(navigationRange.start, tickForX(0));
    const auto visibleEnd = std::min(
        navigationRange.end, tickForX(getWidth()) + gridTicks_);
    domain::Tick firstGridTick = visibleStart / gridTicks_ * gridTicks_;
    if (firstGridTick < visibleStart) firstGridTick += gridTicks_;
    if (navigationRange.start < firstGridTick && visibleStart == navigationRange.start) {
        graphics.setColour(theme::border);
        graphics.drawVerticalLine(
            xForTick(navigationRange.start), 0.0F, static_cast<float>(getHeight()));
    }
    for (domain::Tick tick = firstGridTick; tick <= visibleEnd; tick += gridTicks_) {
        const int x = xForTick(tick);
        const bool barLine = tick % (kBeatTicks * 4) == 0;
        const bool beatLine = tick % kBeatTicks == 0;
        graphics.setColour(barLine ? theme::border
                                   : theme::border.withAlpha(beatLine ? 0.55F : 0.25F));
        graphics.drawVerticalLine(x, 0.0F, static_cast<float>(getHeight()));
    }

    // Notes.
    for (const auto& note : clip->notes) {
        domain::NoteEvent displayed = note;
        const bool selected = isSelected(note.id);
        if (selected && dragMode_ == DragMode::move) {
            displayed.start += dragDeltaTick_;
            displayed.pitch += dragDeltaPitch_;
        } else if (selected && dragMode_ == DragMode::resize
                   && selectedNoteIds_.size() == 1) {
            displayed.duration = dragPreviewDuration_;
        }
        const auto bounds = boundsForNote(displayed);
        graphics.setColour(selected ? theme::selection : theme::success);
        graphics.fillRect(bounds.reduced(0, 1));
        graphics.setColour(theme::canvas);
        graphics.drawRect(bounds.reduced(0, 1), 1);
    }

    if (dragMode_ == DragMode::marquee && dragDidMove_) {
        const auto selection = marqueeBounds();
        graphics.setColour(theme::selection.withAlpha(0.16F));
        graphics.fillRect(selection);
        graphics.setColour(theme::selection);
        graphics.drawRect(selection, 1);
    }

    // Preview notes are visual-only. Apply turns them into ordinary NoteEvents through the command
    // layer; Cancel simply clears this overlay without touching history.
    graphics.setColour(theme::selection.withAlpha(0.42F));
    for (const auto& note : rhythmPreview_) {
        const domain::NoteEvent preview{{}, note.start, note.duration, note.pitch, note.velocity};
        const auto bounds = boundsForNote(preview).reduced(1, 2);
        graphics.fillRect(bounds);
        graphics.drawRect(bounds, 1);
    }
}

void PianoRoll::resized() {
    rowHeight_ = std::max(8, getHeight() / kPitchCount);
    viewport_.setViewportWidth(getWidth());
}

void PianoRoll::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    const auto* clip = currentClip();
    if (clip == nullptr) {
        return;
    }

    const auto hit = noteAt(event.getPosition());
    if (!hit.has_value()) {
        insertionTick_ = viewport_.snapForInsertion(
            tickForX(event.x), gridTicks_, event.mods.isAltDown());
        marqueeToggle_ = event.mods.isShiftDown();
        marqueeBaseSelection_ = marqueeToggle_ ? selectedNoteIds_
                                                : std::vector<domain::EntityId>{};
        selectedNoteIds_ = marqueeBaseSelection_;
        marqueeAnchor_ = event.getPosition();
        marqueeCurrent_ = marqueeAnchor_;
        dragDidMove_ = false;
        dragMode_ = DragMode::marquee;
        repaint();
        return;
    }

    if (event.mods.isShiftDown()) {
        const auto position = std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), *hit);
        if (position == selectedNoteIds_.end()) {
            selectedNoteIds_.push_back(*hit);
        } else {
            selectedNoteIds_.erase(position);
        }
        dragMode_ = DragMode::none;
        repaint();
        return;
    }

    if (!isSelected(*hit)) selectedNoteIds_ = {*hit};
    dragOriginalNotes_.clear();
    for (const auto& note : clip->notes) {
        if (isSelected(note.id)) dragOriginalNotes_.push_back(note);
    }

    // Capture the clicked note as the snap anchor while preserving the full selection.
    for (const auto& note : clip->notes) {
        if (note.id == *hit) {
            insertionTick_ = note.start;
            dragOriginalStart_ = note.start;
            dragOriginalDuration_ = note.duration;
            const auto bounds = boundsForNote(note);
            const bool onRightEdge = event.x >= bounds.getRight() - kResizeHandlePx;
            dragMode_ = onRightEdge && selectedNoteIds_.size() == 1
                ? DragMode::resize : DragMode::move;
            break;
        }
    }
    dragStartTick_ = tickForX(event.x);
    dragStartPitch_ = pitchForY(event.y);
    dragDeltaTick_ = 0;
    dragDeltaPitch_ = 0;
    dragPreviewDuration_ = dragOriginalDuration_;
    dragDidMove_ = false;
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event) {
    if (dragMode_ == DragMode::none) {
        return;
    }
    dragDidMove_ = event.getDistanceFromDragStart() > 2;

    if (dragMode_ == DragMode::marquee) {
        marqueeCurrent_ = event.getPosition();
        updateMarqueeSelection();
        repaint();
        return;
    }
    if (selectedNoteIds_.empty() || dragOriginalNotes_.empty()) return;

    const bool suspendSnap = event.mods.isAltDown();

    if (dragMode_ == DragMode::move) {
        const domain::Tick pointerDelta = tickForX(event.x) - dragStartTick_;
        dragDeltaTick_ = snap(dragOriginalStart_ + pointerDelta, suspendSnap)
            - dragOriginalStart_;
        int pointerPitchDelta = static_cast<int>(pitchForY(event.y))
            - static_cast<int>(dragStartPitch_);
        domain::Tick earliestStart = dragOriginalNotes_.front().start;
        domain::Tick latestEnd = dragOriginalNotes_.front().end();
        int lowestPitch = dragOriginalNotes_.front().pitch;
        int highestPitch = dragOriginalNotes_.front().pitch;
        for (const auto& note : dragOriginalNotes_) {
            earliestStart = std::min(earliestStart, note.start);
            latestEnd = std::max(latestEnd, note.end());
            lowestPitch = std::min(lowestPitch, static_cast<int>(note.pitch));
            highestPitch = std::max(highestPitch, static_cast<int>(note.pitch));
        }
        const auto navigationRange = viewport_.range();
        dragDeltaTick_ = std::clamp(dragDeltaTick_,
            navigationRange.start - earliestStart, navigationRange.end - latestEnd);
        dragDeltaPitch_ = std::clamp(pointerPitchDelta,
            kLowPitch - lowestPitch, kHighPitch - highestPitch);
        repaint();
    } else if (dragMode_ == DragMode::resize) {
        const domain::Tick pointerTick = snap(tickForX(event.x), suspendSnap);
        dragPreviewDuration_ = std::max<domain::Tick>(
            std::max<domain::Tick>(1, gridTicks_), pointerTick - dragOriginalStart_);
        dragPreviewDuration_ = std::min(
            dragPreviewDuration_, viewport_.range().end - dragOriginalStart_);
        repaint();
    }
}

void PianoRoll::mouseUp(const juce::MouseEvent&) {
    bool changed = false;
    if (dragMode_ == DragMode::move && dragDidMove_
        && (dragDeltaTick_ != 0 || dragDeltaPitch_ != 0)) {
        changed = ok(editor_.moveNotes(
            trackId_, clipId_, selectedNoteIds_, dragDeltaTick_, dragDeltaPitch_));
    } else if (dragMode_ == DragMode::resize && dragDidMove_
               && selectedNoteIds_.size() == 1
               && dragPreviewDuration_ != dragOriginalDuration_) {
        changed = ok(editor_.resizeNote(
            trackId_, clipId_, selectedNoteIds_.front(), dragPreviewDuration_));
    }
    dragMode_ = DragMode::none;
    dragOriginalNotes_.clear();
    dragDeltaTick_ = 0;
    dragDeltaPitch_ = 0;
    if (changed) notifyProjectChanged();
    else repaint();
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& event) {
    const auto* clip = currentClip();
    if (clip == nullptr) {
        return;
    }
    if (noteAt(event.getPosition()).has_value()) {
        return;  // double-click on a note does not add another
    }

    const domain::Tick start = viewport_.snapForInsertion(
        tickForX(event.x), gridTicks_, event.mods.isAltDown());
    insertionTick_ = start;
    const domain::Pitch pitch = pitchForY(event.y);
    const domain::Tick duration = gridTicks_;
    const auto result = editor_.addNote(trackId_, clipId_, start, duration, pitch, 100);
    if (ok(result)) {
        selectedNoteIds_ = {std::get<domain::EntityId>(result)};
        dragMode_ = DragMode::none;
        notifyProjectChanged();
    }
}

void PianoRoll::zoomAt(const int anchorX, const double scaleFactor) {
    if (viewport_.zoomAt(anchorX, scaleFactor)) repaint();
}

void PianoRoll::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCommandDown() || event.mods.isCtrlDown()) {
        if (wheel.deltaY != 0.0F) {
            zoomAt(event.x, std::pow(kWheelZoomBase, static_cast<double>(wheel.deltaY)));
        }
        return;
    }

    const float horizontalDelta = wheel.deltaX != 0.0F
        ? wheel.deltaX
        : (event.mods.isShiftDown() ? wheel.deltaY : 0.0F);
    if (horizontalDelta != 0.0F) {
        if (viewport_.scrollByPixels(
                -static_cast<double>(horizontalDelta) * kWheelScrollPixels)) {
            repaint();
        }
        return;
    }
    juce::Component::mouseWheelMove(event, wheel);
}

void PianoRoll::mouseMagnify(const juce::MouseEvent& event, const float scaleFactor) {
    zoomAt(event.x, static_cast<double>(scaleFactor));
}

bool PianoRoll::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        selectedNoteIds_.clear();
        if (const auto* clip = currentClip(); clip != nullptr) {
            for (const auto& note : clip->notes) selectedNoteIds_.push_back(note.id);
        }
        repaint();
        return true;
    }
    if (key == juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0)) {
        copySelection();
        return true;
    }
    if (key == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)) {
        pasteClipboard();
        return true;
    }
    if (key == juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0)) {
        if (onChordShortcut) onChordShortcut();
        return true;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && !selectedNoteIds_.empty()) {
        const auto result = editor_.deleteNotes(trackId_, clipId_, selectedNoteIds_);
        if (ok(result)) {
            selectedNoteIds_.clear();
            notifyProjectChanged();
        }
        return true;
    }
    // Ctrl+Z / Ctrl+Y undo-redo within the roll.
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        editor_.undo();
        selectedNoteIds_.clear();
        notifyProjectChanged();
        return true;
    }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0)) {
        editor_.redo();
        selectedNoteIds_.clear();
        notifyProjectChanged();
        return true;
    }
    return false;
}

}  // namespace composer::ui
