#include "app/MainComponent.h"

#include "domain/Instrument.h"
#include "persistence/MidiFile.h"
#include "persistence/ProjectFile.h"
#include "recording/WavFile.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <variant>
#include <vector>

namespace composer::app {
namespace {

[[nodiscard]] std::filesystem::path projectPath(const juce::File& file) {
    return std::filesystem::path{file.getFullPathName().toWideCharPointer()};
}

}  // namespace

MainComponent::MainComponent() {
    // Load the canonical verse into an editable session. New edits use the session id source so
    // their ids never collide with the fixture's "verse-" ids.
    editor_ = std::make_unique<commands::ProjectEditor>(audio::CanonicalVerse::makeProject(), ids_);

    transport_ = std::make_unique<ui::TransportBar>(
        engine_, static_cast<double>(audio::CanonicalVerse::tempoBpm));
    arrangement_ = std::make_unique<ui::ArrangementBar>();
    trackList_ = std::make_unique<ui::TrackList>(*editor_);
    pianoRoll_ = std::make_unique<ui::PianoRoll>(
        *editor_, static_cast<double>(audio::CanonicalVerse::tempoBpm));
    mixer_ = std::make_unique<ui::MixerPanel>(*editor_);
    waveform_ = std::make_unique<ui::WaveformEditor>(*editor_);
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 2, 0, 2, false, false, true, false);
    unsavedAssetRoot_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Composer")
        .getChildFile("unsaved-" + juce::String::toHexString(juce::Time::currentTimeMillis()));

    trackList_->onTrackSelected = [this](const domain::EntityId& trackId) { selectTrack(trackId); };
    trackList_->onTrackRenamed = [this](const domain::EntityId& trackId, const std::string& name) {
        renameTrack(trackId, name);
    };
    trackList_->onTrackDuplicated = [this](const domain::EntityId& trackId) {
        duplicateTrack(trackId);
    };
    trackList_->onTrackDeleteRequested = [this](const domain::EntityId& trackId) {
        requestDeleteTrack(trackId);
    };
    trackList_->onTrackReordered = [this](const domain::EntityId& trackId,
                                         const std::size_t index) {
        reorderTrack(trackId, index);
    };
    trackList_->onTrackArmed = [this](const domain::EntityId& trackId, const bool armed) {
        setAudioTrackArmed(trackId, armed);
    };
    pianoRoll_->onProjectChanged = [this] {
        cancelRhythmPreview();
        cancelChordPreview();
        publishProject();
        refreshProjectViews();
    };
    pianoRoll_->onChordShortcut = [this] { openChordTool(); };
    mixer_->onProjectChanged = [this] {
        publishProject();
        refreshProjectViews();
    };
    arrangement_->onAddInstrument = [this](const std::string& id) { addInstrument(id); };
    arrangement_->onAddAudioTrack = [this] { addAudioTrack(); };
    arrangement_->onPreviewRhythm = [this](const domain::RhythmPattern pattern,
                                           const domain::Tick subdivision,
                                           const std::uint64_t seed) {
        previewRhythm(pattern, subdivision, seed);
    };
    arrangement_->onGridChanged = [this](const domain::Tick subdivision) {
        pianoRoll_->setGridTicks(subdivision);
    };
    arrangement_->onApplyRhythm = [this] { applyRhythm(); };
    arrangement_->onCancelRhythm = [this] { cancelRhythmPreview(); };
    arrangement_->onUndo = [this] {
        editor_->undo();
        cancelRhythmPreview();
        publishProject();
        refreshProjectViews();
    };
    arrangement_->onRedo = [this] {
        editor_->redo();
        cancelRhythmPreview();
        publishProject();
        refreshProjectViews();
    };
    arrangement_->onImportMidi = [this] { importMidi(); };
    arrangement_->onExportMidi = [this] { exportMidi(); };
    arrangement_->onOpenProject = [this] { openProject(); };
    arrangement_->onSaveProject = [this] { saveProject(); };
    arrangement_->onNewEmptyProject = [this] { createEmptyProject(); };
    arrangement_->onLoadBandSketch = [this] { loadBandSketch(); };
    arrangement_->onOpenChord = [this] { openChordTool(); };
    transport_->onTempoChanged = [this](const double bpm) {
        if (recording_.state() != recording::RecordingState::idle) {
            arrangement_->setStatus("Tempo changes are blocked while recording.", true);
            transport_->setTempoBpm(editor_->project().tempoMap.beatsPerMinute());
            return;
        }
        const auto result = editor_->setTempoBpm(bpm);
        if (std::holds_alternative<application::Error>(result)) {
            showError(std::get<application::Error>(result));
            transport_->setTempoBpm(editor_->project().tempoMap.beatsPerMinute());
            return;
        }
        cancelRhythmPreview();
        cancelChordPreview();
        publishProject();
        refreshProjectViews();
    };
    transport_->onRecordClicked = [this] { toggleRecording(); };
    waveform_->onProjectChanged = [this] {
        publishProject();
        refreshProjectViews();
    };
    waveform_->onStatus = [this](const juce::String& text, const bool isError) {
        arrangement_->setStatus(text, isError);
    };

    addAndMakeVisible(*transport_);
    addAndMakeVisible(*arrangement_);
    addAndMakeVisible(*trackList_);
    addAndMakeVisible(*pianoRoll_);
    addAndMakeVisible(*mixer_);
    addAndMakeVisible(*waveform_);
    addAndMakeVisible(*deviceSelector_);
    waveform_->setVisible(false);
    deviceSelector_->setVisible(false);
    setProjectAssetRoot(projectAssetRoot());

    publishProject();
    if (!editor_->project().tracks.empty()) {
        selectTrack(editor_->project().tracks.front().id);
    }

    // setSize triggers a synchronous resized() callback, so the child components above must already
    // exist before this runs. Calling setSize first would dereference null children and crash on
    // startup (0xC0000005) before the window ever appears.
    setSize(1280, 800);

    setAudioChannels(2, 2);
    deviceReady_ = deviceManager.getCurrentAudioDevice() != nullptr;
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::selectTrack(const domain::EntityId& trackId) {
    for (const auto& track : editor_->project().tracks) {
        if (track.id == trackId && !track.clips.empty()) {
            selectedTrackId_ = trackId;
            selectedClipId_ = track.clips.front().id;
            trackList_->selectTrack(trackId);
            pianoRoll_->setVisible(true);
            waveform_->setVisible(false);
            deviceSelector_->setVisible(false);
            pianoRoll_->showClip(selectedTrackId_, selectedClipId_);
            mixer_->setTrack(selectedTrackId_);
            if (const auto instrument = domain::findInstrument(track.instrumentId)) {
                arrangement_->setInstrumentRole(instrument->role);
            }
            return;
        }
    }
    for (const auto& track : editor_->project().audioTracks) {
        if (track.id == trackId) {
            selectedTrackId_ = trackId;
            selectedClipId_ = track.clips.empty() ? domain::EntityId{} : track.clips.front().id;
            trackList_->selectTrack(trackId);
            pianoRoll_->setVisible(false);
            waveform_->setVisible(true);
            deviceSelector_->setVisible(true);
            waveform_->showTrack(trackId);
            mixer_->setTrack({});
            arrangement_->setStatus("Audio track selected. Arm it, choose an input, then Record.");
            resized();
            return;
        }
    }
    selectedTrackId_ = {};
    selectedClipId_ = {};
    pianoRoll_->setVisible(true);
    waveform_->setVisible(false);
    deviceSelector_->setVisible(false);
    pianoRoll_->showClip({}, {});
    waveform_->showTrack({});
    mixer_->setTrack({});
}

void MainComponent::publishProject() {
    engine_.setAssetRoot(projectAssetRoot());
    engine_.setProject(editor_->project(), editor_->revision());
}

void MainComponent::refreshProjectViews() {
    trackList_->refreshFromProject();
    pianoRoll_->refreshFromProject();
    mixer_->refreshFromProject();
    waveform_->refreshFromProject();
    transport_->setTempoBpm(editor_->project().tempoMap.beatsPerMinute());
    arrangement_->setUndoRedoEnabled(editor_->canUndo(), editor_->canRedo());
}

void MainComponent::addInstrument(const std::string& instrumentId) {
    const auto definition = domain::findInstrument(instrumentId);
    if (!definition.has_value()) {
        showError({application::ErrorCode::invalidArgument, "unknown built-in instrument"});
        return;
    }
    int count = 1;
    for (const auto& track : editor_->project().tracks) {
        if (track.instrumentId == instrumentId) ++count;
    }
    const std::string name = std::string{definition->displayName} + " " + std::to_string(count);
    const auto trackResult = editor_->addInstrumentTrackWithMidiClip(name, instrumentId,
        "Verse", domain::HalfOpenTickRange{0, domain::kPpq * 4 * 8});
    if (std::holds_alternative<application::Error>(trackResult)) {
        showError(std::get<application::Error>(trackResult));
        return;
    }
    const auto trackId = std::get<domain::EntityId>(trackResult);
    publishProject();
    refreshProjectViews();
    selectTrack(trackId);
    arrangement_->setStatus(juce::String{name} + " added.");
}

void MainComponent::addAudioTrack() {
    const int count = static_cast<int>(editor_->project().audioTracks.size()) + 1;
    const auto* device = deviceManager.getCurrentAudioDevice();
    const std::string deviceName = device != nullptr ? device->getName().toStdString() : "default";
    const auto result = editor_->addAudioTrack("Audio " + std::to_string(count), deviceName, 0);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    const auto trackId = std::get<domain::EntityId>(result);
    publishProject();
    refreshProjectViews();
    selectTrack(trackId);
    arrangement_->setStatus("Audio track added. Choose an input, arm it, then Record.");
}

void MainComponent::setAudioTrackArmed(const domain::EntityId& trackId, const bool armed) {
    const auto result = editor_->setAudioTrackArmed(trackId, armed);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    refreshProjectViews();
    arrangement_->setStatus(armed ? "Audio track armed for recording." : "Audio track disarmed.");
}

void MainComponent::renameTrack(
    const domain::EntityId& trackId, const std::string& name) {
    const auto result = editor_->renameTrack(trackId, name);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    publishProject();
    refreshProjectViews();
    arrangement_->setStatus("Track renamed.");
}

void MainComponent::duplicateTrack(const domain::EntityId& trackId) {
    const auto result = editor_->duplicateTrack(trackId);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    const auto duplicateId = std::get<domain::EntityId>(result);
    publishProject();
    refreshProjectViews();
    selectTrack(duplicateId);
    arrangement_->setStatus("Track duplicated with independent clip and note IDs.");
}

void MainComponent::requestDeleteTrack(const domain::EntityId& trackId) {
    juce::Component::SafePointer<MainComponent> safe{this};
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
        "Delete instrument track?",
        "The selected track and its MIDI notes will be removed. Undo restores everything.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create([safe, trackId](const int result) mutable {
            if (safe != nullptr && result != 0) safe->deleteTrack(trackId);
        }));
}

void MainComponent::deleteTrack(const domain::EntityId& trackId) {
    const auto& tracks = editor_->project().tracks;
    const auto position = std::find_if(tracks.begin(), tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    const std::size_t oldIndex = position == tracks.end()
        ? 0
        : static_cast<std::size_t>(std::distance(tracks.begin(), position));
    const auto result = editor_->removeTrack(trackId);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    cancelRhythmPreview();
    cancelChordPreview();
    publishProject();
    refreshProjectViews();
    if (!editor_->project().tracks.empty()) {
        const auto nextIndex = std::min(oldIndex, editor_->project().tracks.size() - 1);
        selectTrack(editor_->project().tracks[nextIndex].id);
    } else if (!editor_->project().audioTracks.empty()) {
        selectTrack(editor_->project().audioTracks.front().id);
    } else {
        selectTrack({});
    }
    arrangement_->setStatus("Track deleted. Undo restores it.");
}

void MainComponent::reorderTrack(
    const domain::EntityId& trackId, const std::size_t newIndex) {
    const auto result = editor_->reorderTrack(trackId, newIndex);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    publishProject();
    refreshProjectViews();
    selectTrack(trackId);
    arrangement_->setStatus("Track order changed.");
}

void MainComponent::previewRhythm(
    const domain::RhythmPattern pattern, const domain::Tick subdivision,
    const std::uint64_t seed) {
    const auto sources = pianoRoll_->rhythmSourceNoteIds();
    if (sources.empty() || selectedTrackId_.empty() || selectedClipId_.empty()) {
        arrangement_->setStatus("Select a note or chord hit before previewing rhythm.", true);
        return;
    }
    commands::ProjectEditor::RhythmApply request;
    request.pattern = pattern;
    request.subdivision = subdivision;
    request.seed = seed;
    request.replaceSource = true;
    request.expectedRevision = editor_->revision();
    request.sourceNoteIds = sources;
    const auto result = editor_->previewRhythm(selectedTrackId_, selectedClipId_, request);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    pianoRoll_->showRhythmPreview(std::get<std::vector<domain::GeneratedNote>>(result));
    rhythmPreviewRequest_ = std::move(request);
    arrangement_->setStatus("Preview is visual only. Apply commits one undo step.");
}

void MainComponent::applyRhythm() {
    if (!rhythmPreviewRequest_.has_value()) {
        arrangement_->setStatus("Preview a rhythm before applying it.", true);
        return;
    }
    const auto result = editor_->applyRhythm(
        selectedTrackId_, selectedClipId_, *rhythmPreviewRequest_);
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        cancelRhythmPreview();
        return;
    }
    cancelRhythmPreview();
    publishProject();
    refreshProjectViews();
    arrangement_->setStatus("Rhythm applied as editable MIDI notes.");
}

void MainComponent::cancelRhythmPreview() {
    rhythmPreviewRequest_.reset();
    if (pianoRoll_ != nullptr) pianoRoll_->clearRhythmPreview();
}

void MainComponent::openChordTool() {
    cancelRhythmPreview();
    const auto track = std::find_if(editor_->project().tracks.begin(),
        editor_->project().tracks.end(),
        [&](const domain::InstrumentTrack& candidate) {
            return candidate.id == selectedTrackId_;
        });
    if (track == editor_->project().tracks.end() || selectedClipId_.empty()) {
        arrangement_->setStatus("Select a pitched MIDI track before opening the chord tool.", true);
        return;
    }
    const auto instrument = domain::findInstrument(track->instrumentId);
    if (instrument.has_value() && !instrument->isPitched()) {
        arrangement_->setStatus("Chord insertion is unavailable for drums.", true);
        return;
    }
    const int octave = instrument.has_value() ? instrument->defaultChordOctave : 4;
    auto content = std::make_unique<ui::ChordPopover>(octave, pianoRoll_->gridTicks());
    auto* popover = content.get();
    juce::Component::SafePointer<MainComponent> safe{this};
    popover->onPreview = [safe, popover](const ui::ChordRequest& request) mutable {
        if (safe != nullptr) safe->previewChord(request, *popover);
    };
    popover->onInsert = [safe, popover](const ui::ChordRequest& request) mutable {
        return safe != nullptr && safe->insertChord(request, *popover);
    };
    popover->onCancel = [safe] {
        if (safe != nullptr) safe->cancelChordPreview();
    };
    juce::CallOutBox::launchAsynchronously(
        std::move(content), pianoRoll_->chordAnchorBoundsInParent(), this);
}

void MainComponent::previewChord(
    const ui::ChordRequest& request, ui::ChordPopover& popover) {
    cancelChordPreview();
    const auto result = editor_->previewChord(selectedTrackId_, selectedClipId_,
        pianoRoll_->insertionTick(), request.duration, request.spec);
    if (std::holds_alternative<application::Error>(result)) {
        const auto& error = std::get<application::Error>(result);
        popover.setStatus(juce::String{error.stableId().data()} + ": "
                + juce::String{error.message},
            true);
        return;
    }
    const auto notes = std::get<std::vector<domain::GeneratedNote>>(result);
    pianoRoll_->showRhythmPreview(notes);
    popover.setStatus("Previewing with the selected track instrument.");

    domain::SequentialIdSource previewIds{"chord-preview"};
    commands::ProjectEditor previewEditor{editor_->project(), previewIds};
    const auto inserted = previewEditor.insertChord(selectedTrackId_, selectedClipId_,
        pianoRoll_->insertionTick(), request.duration, request.spec);
    if (std::holds_alternative<application::Error>(inserted)) return;

    chordPreviewWasPlaying_ = engine_.isPlaying();
    chordPreviewPlayhead_ = engine_.playheadSamples();
    chordAuditionActive_ = true;
    const auto token = ++chordPreviewToken_;
    engine_.setProject(previewEditor.project(), editor_->revision());
    engine_.setPlayheadSamples(editor_->project().tempoMap.ticksToSamples(
        pianoRoll_->insertionTick()));
    engine_.play();
    const double seconds = static_cast<double>(request.duration) * 60.0
        / (editor_->project().tempoMap.beatsPerMinute() * domain::kPpq);
    const int milliseconds = std::clamp(static_cast<int>(seconds * 1000.0), 250, 4000);
    juce::Component::SafePointer<MainComponent> safe{this};
    juce::Timer::callAfterDelay(milliseconds, [safe, token] {
        if (safe != nullptr && safe->chordPreviewToken_ == token) {
            safe->cancelChordPreview();
        }
    });
}

bool MainComponent::insertChord(
    const ui::ChordRequest& request, ui::ChordPopover& popover) {
    cancelChordPreview();
    const auto result = editor_->insertChord(selectedTrackId_, selectedClipId_,
        pianoRoll_->insertionTick(), request.duration, request.spec);
    if (std::holds_alternative<application::Error>(result)) {
        const auto& error = std::get<application::Error>(result);
        popover.setStatus(juce::String{error.stableId().data()} + ": "
                + juce::String{error.message},
            true);
        return false;
    }
    publishProject();
    refreshProjectViews();
    arrangement_->setStatus("Chord inserted as editable MIDI notes in one Undo step.");
    return true;
}

void MainComponent::cancelChordPreview() {
    ++chordPreviewToken_;
    pianoRoll_->clearRhythmPreview();
    if (!chordAuditionActive_) return;
    chordAuditionActive_ = false;
    engine_.stop();
    publishProject();
    engine_.setPlayheadSamples(chordPreviewPlayhead_);
    if (chordPreviewWasPlaying_) engine_.play();
}

void MainComponent::showError(const application::Error& error) {
    arrangement_->setStatus(
        juce::String{error.stableId().data()} + ": " + juce::String{error.message}, true);
}

const domain::AudioTrack* MainComponent::armedAudioTrack() const {
    const auto position = std::find_if(editor_->project().audioTracks.begin(),
        editor_->project().audioTracks.end(),
        [](const domain::AudioTrack& track) { return track.recordArmed; });
    return position == editor_->project().audioTracks.end() ? nullptr : &*position;
}

std::filesystem::path MainComponent::projectAssetRoot() const {
    const juce::File root = projectFile_ != juce::File{} ? projectFile_.getParentDirectory()
                                                         : unsavedAssetRoot_;
    return projectPath(root);
}

void MainComponent::setProjectAssetRoot(const std::filesystem::path& root) {
    engine_.setAssetRoot(root);
    if (waveform_ != nullptr) waveform_->setAssetRoot(root);
}

void MainComponent::copyAudioAssetsTo(const std::filesystem::path& newRoot) {
    const auto oldRoot = projectAssetRoot();
    if (oldRoot == newRoot) return;
    for (const auto& track : editor_->project().audioTracks) {
        for (const auto& clip : track.clips) {
            const std::filesystem::path relative{clip.assetPath};
            if (relative.empty() || relative.is_absolute()) continue;
            const auto source = oldRoot / relative;
            const auto destination = newRoot / relative;
            std::error_code error;
            if (!std::filesystem::exists(source, error)) continue;
            std::filesystem::create_directories(destination.parent_path(), error);
            error.clear();
            std::filesystem::copy_file(source, destination,
                std::filesystem::copy_options::overwrite_existing, error);
        }
    }
}

void MainComponent::recoverPendingRecordings(const std::filesystem::path& root) {
    auto recovered = recording::RecordingSession::recoverPending(root);
    if (std::holds_alternative<application::Error>(recovered)) {
        showError(std::get<application::Error>(recovered));
        return;
    }
    int count = 0;
    for (const auto& take : std::get<std::vector<recording::RecordedTake>>(recovered)) {
        const auto result = editor_->addRecordedAudioClip(take.trackId, "Recovered take",
            take.assetPath, take.startSample, take.frames, take.sampleRate, take.channels);
        if (std::holds_alternative<application::Error>(result)) {
            showError(std::get<application::Error>(result));
            continue;
        }
        ++count;
    }
    if (count > 0) {
        arrangement_->setStatus(juce::String{count} + " recovered recording(s) restored.");
    }
}

void MainComponent::toggleRecording() {
    if (recording_.state() != recording::RecordingState::idle) {
        stopRecording();
        return;
    }
    const auto* track = armedAudioTrack();
    if (track == nullptr) {
        arrangement_->setStatus("Arm an audio track before recording.", true);
        return;
    }
    const auto root = projectAssetRoot();
    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr) {
        const auto reportedLatency = static_cast<std::int64_t>(
            std::max(0, device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()));
        const auto compensation = track->input.latencyCompensationSamples > 0
            ? track->input.latencyCompensationSamples
            : reportedLatency;
        const auto route = editor_->setAudioInputRoute(track->id, device->getName().toStdString(),
            track->input.channelIndex, track->input.monitoring, compensation);
        if (std::holds_alternative<application::Error>(route)) {
            showError(std::get<application::Error>(route));
            return;
        }
        track = armedAudioTrack();
        if (track == nullptr) {
            arrangement_->setStatus("Armed audio track changed before recording could start.", true);
            return;
        }
    }
    recording::RecordingRequest request;
    request.projectRoot = root;
    request.takeId = "take-" + juce::String::toHexString(juce::Time::currentTimeMillis()).toStdString();
    request.trackId = track->id;
    request.sampleRate = std::max(8000.0, engine_.sampleRate());
    request.channels = 1;
    request.firstInputChannel = std::max(0, track->input.channelIndex);
    request.beatsPerMinute = editor_->project().tempoMap.beatsPerMinute();
    request.beatsPerBar = editor_->project().timeSignatureNumerator;
    request.countInBars = 1;
    const auto countInFrames = static_cast<domain::ProjectSample>(std::llround(
        static_cast<double>(request.countInBars * request.beatsPerBar) * 60.0
        / request.beatsPerMinute * request.sampleRate));
    request.startSample = engine_.playheadSamples() + countInFrames;
    request.fifoCapacityFrames = static_cast<std::size_t>(request.sampleRate * 4.0);
    request.expectedDurationSeconds = 60.0;
    const auto started = recording_.start(request);
    if (std::holds_alternative<application::Error>(started)) {
        showError(std::get<application::Error>(started));
        return;
    }
    transport_->setRecording(true);
    engine_.setMetronomeEnabled(true);
    engine_.play();
    arrangement_->setStatus("Recording count-in started. Press Record again to stop.");
}

void MainComponent::stopRecording() {
    auto stopped = recording_.stop();
    transport_->setRecording(false);
    if (std::holds_alternative<application::Error>(stopped)) {
        showError(std::get<application::Error>(stopped));
        return;
    }
    const auto outcome = std::get<recording::RecordingOutcome>(std::move(stopped));
    const auto clip = editor_->addRecordedAudioClip(outcome.take.trackId, "Recorded take",
        outcome.take.assetPath, outcome.take.startSample, outcome.take.frames,
        outcome.take.sampleRate, outcome.take.channels);
    if (std::holds_alternative<application::Error>(clip)) {
        showError(std::get<application::Error>(clip));
        return;
    }
    publishProject();
    refreshProjectViews();
    selectTrack(outcome.take.trackId);
    if (outcome.failure.has_value()) {
        showError(*outcome.failure);
    } else {
        arrangement_->setStatus("Recording finalized and added as an editable audio clip.");
    }
}

void MainComponent::importMidi() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Import Standard MIDI", juce::File{}, "*.mid;*.midi");
    juce::Component::SafePointer<MainComponent> safe{this};
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) mutable {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            juce::MemoryBlock data;
            if (!file.loadFileAsData(data)) {
                safe->showError({application::ErrorCode::dependencyUnavailable,
                    "could not read the selected MIDI file"});
                return;
            }
            const auto* begin = static_cast<const std::uint8_t*>(data.getData());
            std::vector<std::uint8_t> bytes(begin, begin + data.getSize());
            auto imported = persistence::MidiFile::importBytes(bytes);
            if (std::holds_alternative<application::Error>(imported)) {
                safe->showError(std::get<application::Error>(imported));
                return;
            }
            auto replaced = safe->editor_->replaceProject(
                std::get<domain::Project>(std::move(imported)));
            if (std::holds_alternative<application::Error>(replaced)) {
                safe->showError(std::get<application::Error>(replaced));
                return;
            }
            safe->cancelRhythmPreview();
            safe->publishProject();
            safe->refreshProjectViews();
            if (!safe->editor_->project().tracks.empty()) {
                safe->selectTrack(safe->editor_->project().tracks.front().id);
            }
            safe->arrangement_->setStatus("MIDI imported. Undo restores the previous project.");
        });
}

void MainComponent::exportMidi() {
    auto exported = persistence::MidiFile::exportBytes(editor_->project());
    if (std::holds_alternative<application::Error>(exported)) {
        showError(std::get<application::Error>(exported));
        return;
    }
    auto bytes = std::get<std::vector<std::uint8_t>>(std::move(exported));
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Standard MIDI", juce::File{"composer.mid"}, "*.mid");
    juce::Component::SafePointer<MainComponent> safe{this};
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, bytes = std::move(bytes)](const juce::FileChooser& chooser) mutable {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension("mid")) file = file.withFileExtension("mid");
            auto stream = file.createOutputStream();
            if (stream == nullptr || stream->failedToOpen() || !stream->setPosition(0)
                || !stream->truncate() || !stream->write(bytes.data(), bytes.size())) {
                safe->showError({application::ErrorCode::dependencyUnavailable,
                    "could not write the selected MIDI file"});
                return;
            }
            stream->flush();
            safe->arrangement_->setStatus("MIDI exported: " + file.getFileName());
        });
}

void MainComponent::openProject() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open Composer project", projectFile_, "*.composer.json;*.json");
    juce::Component::SafePointer<MainComponent> safe{this};
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) mutable {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.existsAsFile()) {
                safe->showError({application::ErrorCode::dependencyUnavailable,
                    "the selected project file does not exist"});
                return;
            }
            const auto parsed = persistence::ProjectFile::load(projectPath(file));
            if (std::holds_alternative<application::Error>(parsed)) {
                safe->showError(std::get<application::Error>(parsed));
                return;
            }
            const auto replaced = safe->editor_->replaceProject(
                std::get<domain::Project>(parsed));
            if (std::holds_alternative<application::Error>(replaced)) {
                safe->showError(std::get<application::Error>(replaced));
                return;
            }
            safe->projectFile_ = file;
            safe->setProjectAssetRoot(safe->projectAssetRoot());
            safe->recoverPendingRecordings(safe->projectAssetRoot());
            safe->cancelRhythmPreview();
            safe->cancelChordPreview();
            safe->publishProject();
            safe->refreshProjectViews();
            if (!safe->editor_->project().tracks.empty()) {
                safe->selectTrack(safe->editor_->project().tracks.front().id);
            } else if (!safe->editor_->project().audioTracks.empty()) {
                safe->selectTrack(safe->editor_->project().audioTracks.front().id);
            } else {
                safe->selectTrack({});
            }
            safe->arrangement_->setStatus("Project opened. Undo restores the previous session.");
        });
}

void MainComponent::saveProject() {
    if (projectFile_ != juce::File{}) {
        saveProjectTo(projectFile_);
        return;
    }
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Composer project", juce::File{"song.composer.json"}, "*.composer.json");
    juce::Component::SafePointer<MainComponent> safe{this};
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& chooser) mutable {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!file.getFileName().endsWithIgnoreCase(".composer.json")) {
                file = file.getSiblingFile(file.getFileName() + ".composer.json");
            }
            safe->saveProjectTo(file);
        });
}

void MainComponent::saveProjectTo(const juce::File& file) {
    copyAudioAssetsTo(projectPath(file.getParentDirectory()));
    const auto result = persistence::ProjectFile::save(projectPath(file), editor_->project());
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    projectFile_ = file;
    setProjectAssetRoot(projectAssetRoot());
    arrangement_->setStatus("Project saved: " + file.getFileName());
}

void MainComponent::createEmptyProject() {
    domain::Project project;
    project.id = ids_.next();
    project.name = "Untitled";
    const auto result = editor_->replaceProject(std::move(project));
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    projectFile_ = {};
    unsavedAssetRoot_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Composer")
        .getChildFile("unsaved-" + juce::String::toHexString(juce::Time::currentTimeMillis()));
    setProjectAssetRoot(projectAssetRoot());
    cancelRhythmPreview();
    cancelChordPreview();
    publishProject();
    refreshProjectViews();
    selectTrack({});
    arrangement_->setStatus("Empty project created. Add any built-in instrument.");
}

void MainComponent::loadBandSketch() {
    const auto result = editor_->replaceProject(audio::CanonicalVerse::makeProject());
    if (std::holds_alternative<application::Error>(result)) {
        showError(std::get<application::Error>(result));
        return;
    }
    projectFile_ = {};
    unsavedAssetRoot_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Composer")
        .getChildFile("unsaved-" + juce::String::toHexString(juce::Time::currentTimeMillis()));
    setProjectAssetRoot(projectAssetRoot());
    cancelRhythmPreview();
    cancelChordPreview();
    publishProject();
    refreshProjectViews();
    selectTrack(editor_->project().tracks.front().id);
    arrangement_->setStatus("Band sketch template loaded. Undo restores the previous project.");
}

void MainComponent::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) {
    engine_.prepare({
        .sampleRate = sampleRate,
        .maximumBlockSize = static_cast<std::size_t>(samplesPerBlockExpected),
        .outputChannels = 2});
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    const auto availableChannels = bufferToFill.buffer->getNumChannels();
    if (availableChannels <= 0) {
        return;
    }
    std::array<const float*, 8> inputs{};
    const auto inputChannels = static_cast<std::size_t>(std::min(availableChannels,
        static_cast<int>(inputs.size())));
    for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        inputs[channel] = bufferToFill.buffer->getReadPointer(
            static_cast<int>(channel), bufferToFill.startSample);
    }
    recording_.processInput(inputs.data(), inputChannels,
        static_cast<std::size_t>(bufferToFill.numSamples));

    const auto channelCount = std::min(availableChannels, 2);
    std::array<float*, 2> outputs{
        bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample),
        channelCount > 1
            ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
            : bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample)};
    // PlaybackEngine owns the playhead; startSample is unused by process() and passed as 0.
    engine_.process({
        .outputs = outputs.data(),
        .outputChannels = static_cast<std::size_t>(channelCount),
        .frameCount = static_cast<std::size_t>(bufferToFill.numSamples),
        .startSample = 0});

    const auto* track = armedAudioTrack();
    if (track != nullptr && track->input.monitoring == domain::MonitoringMode::software
        && inputChannels > static_cast<std::size_t>(track->input.channelIndex)) {
        const auto* input = inputs[static_cast<std::size_t>(track->input.channelIndex)];
        if (input != nullptr) {
            for (int frame = 0; frame < bufferToFill.numSamples; ++frame) {
                const float monitored = input[frame] * 0.7F;
                for (int channel = 0; channel < channelCount; ++channel) {
                    outputs[static_cast<std::size_t>(channel)][frame] = std::clamp(
                        outputs[static_cast<std::size_t>(channel)][frame] + monitored,
                        -1.0F, 1.0F);
                }
            }
        }
    }
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(ui::theme::canvas);
}

void MainComponent::resized() {
    // resized() can fire before the child components are constructed (e.g. from a setSize call
    // early in the constructor). Guard against null children so layout is safe in any order.
    if (transport_ == nullptr || arrangement_ == nullptr || trackList_ == nullptr
        || pianoRoll_ == nullptr || mixer_ == nullptr || waveform_ == nullptr
        || deviceSelector_ == nullptr) {
        return;
    }
    auto area = getLocalBounds();
    transport_->setBounds(area.removeFromTop(56));
    arrangement_->setBounds(area.removeFromTop(78));
    mixer_->setBounds(area.removeFromBottom(76));
    trackList_->setBounds(area.removeFromLeft(260));
    if (waveform_->isVisible()) {
        deviceSelector_->setBounds(area.removeFromRight(280).reduced(6));
        waveform_->setBounds(area);
        pianoRoll_->setBounds({});
    } else {
        deviceSelector_->setBounds({});
        pianoRoll_->setBounds(area);
        waveform_->setBounds(area);
    }
}

}  // namespace composer::app
