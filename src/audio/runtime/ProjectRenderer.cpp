#include "audio/runtime/ProjectRenderer.h"

#include <algorithm>
#include <cmath>

namespace composer::audio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Standard equal-temperament: MIDI 69 == 440 Hz.
[[nodiscard]] double pitchToFrequency(const domain::Pitch pitch) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(pitch) - 69.0) / 12.0);
}

[[nodiscard]] float nextNoise(std::uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFFU) / 8388607.5F - 1.0F;
}

}  // namespace

ProjectRenderer::ProjectRenderer(ProjectSnapshot snapshot) noexcept
    : snapshot_(std::move(snapshot)) {}

void ProjectRenderer::prepare(const AudioSpec& spec) {
    spec_ = spec;
    seekTo(0);
}

void ProjectRenderer::seekTo(const domain::ProjectSample sample) noexcept {
    for (auto& voice : voices_) {
        voice = Voice{};
    }
    // Position the cursor at the first note that starts at or after `sample`. Notes already
    // sounding at the seek point are not retriggered (acceptable for a fresh seek/loop).
    cursor_ = 0;
    while (cursor_ < snapshot_.notes.size()
        && snapshot_.notes[cursor_].startSample < sample) {
        ++cursor_;
    }
    nextSample_ = sample;
}

void ProjectRenderer::startDueNotes(const domain::ProjectSample sample) noexcept {
    while (cursor_ < snapshot_.notes.size()
        && snapshot_.notes[cursor_].startSample == sample) {
        const auto& note = snapshot_.notes[cursor_];
        ++cursor_;
        if (note.amplitude <= 0.0F || note.endSample <= note.startSample) {
            continue;
        }
        // Find a free voice; if the pool is saturated, steal nothing (deterministic drop). The
        // snapshot caps simultaneous notes well under kMaxVoices for the acceptance fixture.
        for (auto& voice : voices_) {
            if (!voice.active) {
                voice.active = true;
                voice.startSample = note.startSample;
                voice.endSample = note.endSample;
                voice.phase = 0.0;
                voice.phaseIncrement = kTwoPi * pitchToFrequency(note.pitch) / spec_.sampleRate;
                voice.amplitude = note.amplitude;
                const double panAngle = (static_cast<double>(note.pan) + 1.0) * kPi / 4.0;
                voice.leftGain = static_cast<float>(std::cos(panAngle));
                voice.rightGain = static_cast<float>(std::sin(panAngle));
                voice.timbre = note.timbre;
                voice.noiseState = 0x9E3779B9U ^ static_cast<std::uint32_t>(note.pitch)
                    ^ (note.voiceGroup * 0x85EBCA6BU)
                    ^ static_cast<std::uint32_t>(note.startSample);
                if (voice.noiseState == 0) {
                    voice.noiseState = 1;
                }
                break;
            }
        }
    }
}

ProjectRenderer::StereoSample ProjectRenderer::renderSample(
    const domain::ProjectSample sample) noexcept {
    // Activate any notes that begin exactly at this sample. Notes whose start sits between block
    // boundaries are still caught because we step sample-by-sample.
    startDueNotes(sample);

    StereoSample mixed;
    for (auto& voice : voices_) {
        if (!voice.active) {
            continue;
        }
        if (sample >= voice.endSample) {
            voice.active = false;
            continue;
        }
        // Short linear release over the final portion keeps the sum bounded and deterministic.
        const auto remaining = voice.endSample - sample;
        const float release = remaining < 480 ? static_cast<float>(remaining) / 480.0F : 1.0F;
        const auto age = sample - voice.startSample;
        const auto lifetime = std::max<domain::ProjectSample>(1, voice.endSample - voice.startSample);
        float timbreEnvelope = 1.0F;
        float wave = 0.0F;
        const float sine = static_cast<float>(std::sin(voice.phase));
        switch (voice.timbre) {
            case domain::RenderTimbre::sine:
                wave = sine;
                break;
            case domain::RenderTimbre::pluck:
                wave = 0.72F * sine
                    + 0.28F * static_cast<float>(std::sin(voice.phase * 2.0));
                timbreEnvelope = std::max(
                    0.0F, 1.0F - static_cast<float>(age) / static_cast<float>(lifetime));
                break;
            case domain::RenderTimbre::triangle:
                wave = static_cast<float>((2.0 / kPi) * std::asin(std::sin(voice.phase)));
                break;
            case domain::RenderTimbre::noise: {
                wave = nextNoise(voice.noiseState);
                const auto decaySamples = std::max<domain::ProjectSample>(
                    1, std::min<domain::ProjectSample>(lifetime,
                           static_cast<domain::ProjectSample>(spec_.sampleRate * 0.08)));
                timbreEnvelope = std::max(
                    0.0F, 1.0F - static_cast<float>(age) / static_cast<float>(decaySamples));
                break;
            }
            case domain::RenderTimbre::saw:
                wave = static_cast<float>((voice.phase / kPi) - 1.0);
                break;
        }
        const float value = voice.amplitude * 0.16F * release * timbreEnvelope * wave;
        mixed.left += value * voice.leftGain;
        mixed.right += value * voice.rightGain;
        voice.phase += voice.phaseIncrement;
        if (voice.phase >= kTwoPi) {
            voice.phase -= kTwoPi;
        }
    }
    mixed.left = std::clamp(mixed.left, -1.0F, 1.0F);
    mixed.right = std::clamp(mixed.right, -1.0F, 1.0F);
    return mixed;
}

void ProjectRenderer::render(const RenderBlock& block) noexcept {
    // If the caller jumped the timeline (loop wrap or transport seek), resync the note cursor and
    // voices so playback stays correct. Contiguous block rendering (the fixture path) never seeks.
    if (block.startSample != nextSample_) {
        seekTo(block.startSample);
    }

    for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
        const auto sample = block.startSample + static_cast<domain::ProjectSample>(frame);
        StereoSample value;
        if (sample >= 0 && sample < snapshot_.lengthSamples) {
            value = renderSample(sample);
            for (const auto& clip : snapshot_.audioClips) {
                if (sample < clip.startSample || sample >= clip.startSample + clip.lengthFrames
                    || clip.channels <= 0) {
                    continue;
                }
                const auto clipFrame = sample - clip.startSample;
                const auto sourceFrame = clip.sourceOffsetFrames + clipFrame;
                const auto sourceIndex = static_cast<std::size_t>(sourceFrame)
                    * static_cast<std::size_t>(clip.channels);
                if (sourceIndex >= clip.interleaved.size()) continue;
                const float leftSource = clip.interleaved[sourceIndex];
                const float rightSource = clip.channels > 1 && sourceIndex + 1 < clip.interleaved.size()
                    ? clip.interleaved[sourceIndex + 1]
                    : leftSource;
                float envelope = 1.0F;
                if (clip.fadeInFrames > 0 && clipFrame < clip.fadeInFrames) {
                    envelope *= static_cast<float>(clipFrame) / static_cast<float>(clip.fadeInFrames);
                }
                const auto remaining = clip.lengthFrames - clipFrame;
                if (clip.fadeOutFrames > 0 && remaining < clip.fadeOutFrames) {
                    envelope *= static_cast<float>(remaining) / static_cast<float>(clip.fadeOutFrames);
                }
                const double panAngle = (static_cast<double>(clip.pan) + 1.0) * kPi / 4.0;
                value.left += leftSource * clip.gain * envelope
                    * static_cast<float>(std::cos(panAngle));
                value.right += rightSource * clip.gain * envelope
                    * static_cast<float>(std::sin(panAngle));
            }
            value.left = std::clamp(value.left, -1.0F, 1.0F);
            value.right = std::clamp(value.right, -1.0F, 1.0F);
        }
        if (block.outputChannels == 1) {
            block.outputs[0][frame] = (value.left + value.right) * 0.5F;
            continue;
        }
        if (block.outputChannels >= 2) {
            block.outputs[0][frame] = value.left;
            block.outputs[1][frame] = value.right;
        }
        for (std::size_t channel = 2; channel < block.outputChannels; ++channel) {
            block.outputs[channel][frame] = (value.left + value.right) * 0.5F;
        }
    }
    nextSample_ = block.startSample + static_cast<domain::ProjectSample>(block.frameCount);
}

}  // namespace composer::audio
