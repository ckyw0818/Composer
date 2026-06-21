#pragma once

#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/PlaybackEngine.h"
#include "commands/ProjectEditor.h"
#include "domain/IdSource.h"
#include "ui/PianoRoll.h"
#include "ui/TrackList.h"
#include "ui/TransportBar.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace composer::app {

// S1 main workspace: transport on top, track headers on the left, piano roll filling the rest.
// The canonical verse is loaded through the command layer; edits replay into the playback engine.
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

    domain::SequentialIdSource ids_{"session"};
    audio::PlaybackEngine engine_;
    std::unique_ptr<commands::ProjectEditor> editor_;

    std::unique_ptr<ui::TransportBar> transport_;
    std::unique_ptr<ui::TrackList> trackList_;
    std::unique_ptr<ui::PianoRoll> pianoRoll_;

    bool deviceReady_{};
};

}  // namespace composer::app
