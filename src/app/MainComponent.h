#pragma once

#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/PlaybackEngine.h"
#include "commands/ProjectEditor.h"
#include "domain/IdSource.h"
#include "domain/Rhythm.h"
#include "ui/ArrangementBar.h"
#include "ui/ChordPopover.h"
#include "ui/MixerPanel.h"
#include "ui/PianoRoll.h"
#include "ui/TrackList.h"
#include "ui/TransportBar.h"
#include "ui/WaveformEditor.h"
#include "recording/RecordingSession.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace composer::app {

// S2 workspace: transport and arrangement commands on top, track headers on the left, piano roll
// in the centre, and mixer basics below. Edits republish immutable playback snapshots.
class MainComponent final : public juce::AudioAppComponent {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void selectTrack(const domain::EntityId& trackId);
    void publishProject();
    void refreshProjectViews();
    void addInstrument(const std::string& instrumentId);
    void addAudioTrack();
    void setAudioTrackArmed(const domain::EntityId& trackId, bool armed);
    void renameTrack(const domain::EntityId& trackId, const std::string& name);
    void duplicateTrack(const domain::EntityId& trackId);
    void requestDeleteTrack(const domain::EntityId& trackId);
    void deleteTrack(const domain::EntityId& trackId);
    void reorderTrack(const domain::EntityId& trackId, std::size_t newIndex);
    void previewRhythm(
        domain::RhythmPattern pattern, domain::Tick subdivision, std::uint64_t seed);
    void applyRhythm();
    void cancelRhythmPreview();
    void openChordTool();
    void previewChord(const ui::ChordRequest& request, ui::ChordPopover& popover);
    [[nodiscard]] bool insertChord(
        const ui::ChordRequest& request, ui::ChordPopover& popover);
    void cancelChordPreview();
    void importMidi();
    void exportMidi();
    void openProject();
    void saveProject();
    void saveProjectTo(const juce::File& file);
    void toggleRecording();
    void stopRecording();
    [[nodiscard]] const domain::AudioTrack* armedAudioTrack() const;
    [[nodiscard]] std::filesystem::path projectAssetRoot() const;
    void setProjectAssetRoot(const std::filesystem::path& root);
    void copyAudioAssetsTo(const std::filesystem::path& newRoot);
    void recoverPendingRecordings(const std::filesystem::path& root);
    void createEmptyProject();
    void loadBandSketch();
    void showError(const application::Error& error);

    domain::SequentialIdSource ids_{"session"};
    audio::PlaybackEngine engine_;
    std::unique_ptr<commands::ProjectEditor> editor_;

    std::unique_ptr<ui::TransportBar> transport_;
    std::unique_ptr<ui::ArrangementBar> arrangement_;
    std::unique_ptr<ui::TrackList> trackList_;
    std::unique_ptr<ui::PianoRoll> pianoRoll_;
    std::unique_ptr<ui::MixerPanel> mixer_;
    std::unique_ptr<ui::WaveformEditor> waveform_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    domain::EntityId selectedTrackId_;
    domain::EntityId selectedClipId_;
    std::optional<commands::ProjectEditor::RhythmApply> rhythmPreviewRequest_;
    juce::File projectFile_;
    juce::File unsavedAssetRoot_;
    std::uint64_t chordPreviewToken_{0};
    bool chordAuditionActive_{false};
    bool chordPreviewWasPlaying_{false};
    domain::ProjectSample chordPreviewPlayhead_{};

    bool deviceReady_{};
    recording::RecordingSession recording_;
};

}  // namespace composer::app
