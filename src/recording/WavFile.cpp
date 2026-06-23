#include "recording/WavFile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>

namespace composer::recording {
namespace {

using application::Error;
using application::ErrorCode;

void putU16(std::array<std::uint8_t, 44>& header, const std::size_t offset,
    const std::uint16_t value) {
    header[offset] = static_cast<std::uint8_t>(value);
    header[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void putU32(std::array<std::uint8_t, 44>& header, const std::size_t offset,
    const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        header[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>(value >> static_cast<unsigned int>(shift));
    }
}

std::uint16_t getU16(const std::array<std::uint8_t, 44>& header, const std::size_t offset) {
    return static_cast<std::uint16_t>(header[offset])
        | static_cast<std::uint16_t>(header[offset + 1] << 8U);
}

std::uint32_t getU32(const std::array<std::uint8_t, 44>& header, const std::size_t offset) {
    std::uint32_t value{};
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(header[offset + static_cast<std::size_t>(shift / 8)])
            << static_cast<unsigned int>(shift);
    }
    return value;
}

std::array<std::uint8_t, 44> makeHeader(
    const std::uint32_t sampleRate, const std::uint16_t channels, const std::uint32_t dataBytes) {
    std::array<std::uint8_t, 44> header{};
    std::memcpy(header.data(), "RIFF", 4);
    putU32(header, 4, 36U + dataBytes);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    putU32(header, 16, 16);
    putU16(header, 20, 3);  // IEEE float
    putU16(header, 22, channels);
    putU32(header, 24, sampleRate);
    putU32(header, 28, sampleRate * channels * 4U);
    putU16(header, 32, static_cast<std::uint16_t>(channels * 4U));
    putU16(header, 34, 32);
    std::memcpy(header.data() + 36, "data", 4);
    putU32(header, 40, dataBytes);
    return header;
}

application::Result<WavInfo> parseHeader(
    const std::array<std::uint8_t, 44>& header, const std::uint64_t fileBytes) {
    if (std::memcmp(header.data(), "RIFF", 4) != 0
        || std::memcmp(header.data() + 8, "WAVEfmt ", 8) != 0
        || std::memcmp(header.data() + 36, "data", 4) != 0) {
        return Error{ErrorCode::recoveryFailure, "recording is not a supported canonical WAV"};
    }
    const auto format = getU16(header, 20);
    const auto channels = getU16(header, 22);
    const auto sampleRate = getU32(header, 24);
    const auto bits = getU16(header, 34);
    const auto frameBytes = static_cast<std::uint64_t>(channels) * (bits / 8U);
    if ((format != 1 && format != 3) || channels == 0 || sampleRate == 0
        || (bits != 16 && bits != 32) || frameBytes == 0 || fileBytes < 44) {
        return Error{ErrorCode::recoveryFailure, "recording WAV header is invalid"};
    }
    const auto declaredBytes = getU32(header, 40);
    const auto availableBytes = fileBytes - 44;
    const auto usableBytes = std::min<std::uint64_t>(declaredBytes, availableBytes);
    return WavInfo{static_cast<double>(sampleRate), static_cast<int>(channels),
        static_cast<std::int64_t>(usableBytes / frameBytes), bits, format == 3};
}

}  // namespace

application::Result<std::monostate> FloatWavWriter::open(
    const std::filesystem::path& path, const double sampleRate, const int channels,
    const std::uint64_t failAfterDataBytes) {
    if (path.empty() || !std::isfinite(sampleRate) || sampleRate < 8000.0
        || sampleRate > 384000.0 || channels <= 0 || channels > 32) {
        return Error{ErrorCode::invalidArgument, "WAV recording settings are invalid"};
    }
    path_ = path;
    channels_ = channels;
    failAfterDataBytes_ = failAfterDataBytes;
    framesWritten_ = 0;
    dataBytesWritten_ = 0;
    stream_.open(path, std::ios::binary | std::ios::trunc);
    if (!stream_) {
        return Error{ErrorCode::recordingPathUnavailable,
            "recording partial file could not be created"};
    }
    const auto header = makeHeader(
        static_cast<std::uint32_t>(std::llround(sampleRate)),
        static_cast<std::uint16_t>(channels), 0);
    stream_.write(reinterpret_cast<const char*>(header.data()),
        static_cast<std::streamsize>(header.size()));
    stream_.flush();
    if (!stream_) {
        closeWithoutFinalize();
        return Error{ErrorCode::audioWriteFailure, "recording WAV header could not be written"};
    }
    return std::monostate{};
}

bool FloatWavWriter::append(const float* const interleaved, const std::size_t frames) noexcept {
    if (!stream_ || interleaved == nullptr || frames == 0) return frames == 0;
    const auto frameBytes = static_cast<std::uint64_t>(channels_) * 4U;
    const auto requestedBytes = static_cast<std::uint64_t>(frames)
        * frameBytes;
    const auto remainingBytes = failAfterDataBytes_ - std::min(failAfterDataBytes_, dataBytesWritten_);
    const auto writableFrames = requestedBytes > remainingBytes
        ? static_cast<std::size_t>(remainingBytes / frameBytes)
        : frames;
    for (std::size_t sample = 0; sample < writableFrames * static_cast<std::size_t>(channels_); ++sample) {
        const auto bits = std::bit_cast<std::uint32_t>(interleaved[sample]);
        const std::array<char, 4> bytes{
            static_cast<char>(bits), static_cast<char>(bits >> 8U),
            static_cast<char>(bits >> 16U), static_cast<char>(bits >> 24U)};
        stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream_) return false;
    }
    dataBytesWritten_ += static_cast<std::uint64_t>(writableFrames) * frameBytes;
    framesWritten_ += static_cast<std::int64_t>(writableFrames);
    return writableFrames == frames;
}

application::Result<std::monostate> FloatWavWriter::finalize() {
    if (!stream_) return Error{ErrorCode::audioWriteFailure, "recording stream is not open"};
    if (dataBytesWritten_ > std::numeric_limits<std::uint32_t>::max()) {
        closeWithoutFinalize();
        return Error{ErrorCode::audioWriteFailure, "recording exceeds canonical WAV size"};
    }
    stream_.flush();
    stream_.seekp(0, std::ios::beg);
    const auto header = makeHeader(0, 1, 0);
    // Preserve the format fields written at open; only patch RIFF and data sizes.
    stream_.seekp(4, std::ios::beg);
    const std::uint32_t riffSize = 36U + static_cast<std::uint32_t>(dataBytesWritten_);
    const std::array<char, 4> riffBytes{
        static_cast<char>(riffSize), static_cast<char>(riffSize >> 8U),
        static_cast<char>(riffSize >> 16U), static_cast<char>(riffSize >> 24U)};
    stream_.write(riffBytes.data(), 4);
    stream_.seekp(40, std::ios::beg);
    const auto dataSize = static_cast<std::uint32_t>(dataBytesWritten_);
    const std::array<char, 4> dataBytes{
        static_cast<char>(dataSize), static_cast<char>(dataSize >> 8U),
        static_cast<char>(dataSize >> 16U), static_cast<char>(dataSize >> 24U)};
    stream_.write(dataBytes.data(), 4);
    stream_.flush();
    const bool ok = static_cast<bool>(stream_);
    stream_.close();
    (void) header;
    return ok ? application::Result<std::monostate>{std::monostate{}}
              : application::Result<std::monostate>{Error{ErrorCode::audioWriteFailure,
                    "recording WAV could not be finalized"}};
}

void FloatWavWriter::closeWithoutFinalize() noexcept {
    if (stream_.is_open()) stream_.close();
}

application::Result<WavInfo> WavFile::inspect(const std::filesystem::path& path) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes < 44) {
        return Error{ErrorCode::recoveryFailure, "recording WAV is missing or truncated"};
    }
    std::ifstream input{path, std::ios::binary};
    std::array<std::uint8_t, 44> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()))) {
        return Error{ErrorCode::recoveryFailure, "recording WAV header could not be read"};
    }
    return parseHeader(header, bytes);
}

application::Result<WavInfo> WavFile::repairFloatHeader(const std::filesystem::path& path) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes < 44 || bytes - 44 > std::numeric_limits<std::uint32_t>::max()) {
        return Error{ErrorCode::recoveryFailure, "recoverable WAV size is invalid"};
    }
    std::fstream stream{path, std::ios::binary | std::ios::in | std::ios::out};
    std::array<std::uint8_t, 44> header{};
    if (!stream.read(reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()))
        || std::memcmp(header.data(), "RIFF", 4) != 0
        || getU16(header, 20) != 3 || getU16(header, 34) != 32) {
        return Error{ErrorCode::recoveryFailure, "partial recording WAV header is not repairable"};
    }
    const auto channels = getU16(header, 22);
    const auto frameBytes = static_cast<std::uint64_t>(channels) * 4U;
    if (channels == 0 || (bytes - 44) % frameBytes != 0) {
        return Error{ErrorCode::recoveryFailure, "partial recording ends inside an audio frame"};
    }
    const auto dataBytes = static_cast<std::uint32_t>(bytes - 44);
    putU32(header, 4, 36U + dataBytes);
    putU32(header, 40, dataBytes);
    stream.seekp(0, std::ios::beg);
    stream.write(reinterpret_cast<const char*>(header.data()),
        static_cast<std::streamsize>(header.size()));
    stream.flush();
    if (!stream) return Error{ErrorCode::recoveryFailure, "partial WAV header repair failed"};
    return parseHeader(header, bytes);
}

application::Result<std::vector<float>> WavFile::readMonoPeaks(
    const std::filesystem::path& path, const std::size_t peakCount) {
    const auto inspected = inspect(path);
    if (std::holds_alternative<Error>(inspected)) return std::get<Error>(inspected);
    const auto info = std::get<WavInfo>(inspected);
    if (!info.floatingPoint || info.bitsPerSample != 32 || peakCount == 0) {
        return Error{ErrorCode::invalidArgument, "waveform peaks require float WAV and non-zero bins"};
    }
    std::ifstream input{path, std::ios::binary};
    input.seekg(44, std::ios::beg);
    std::vector<float> peaks(peakCount, 0.0F);
    const auto framesPerPeak = std::max<std::int64_t>(1,
        (info.frames + static_cast<std::int64_t>(peakCount) - 1)
            / static_cast<std::int64_t>(peakCount));
    for (std::int64_t frame = 0; frame < info.frames; ++frame) {
        float peak{};
        for (int channel = 0; channel < info.channels; ++channel) {
            std::array<std::uint8_t, 4> bytes{};
            if (!input.read(reinterpret_cast<char*>(bytes.data()), 4)) {
                return Error{ErrorCode::recoveryFailure, "waveform source ended unexpectedly"};
            }
            const std::uint32_t bits = static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8U)
                | (static_cast<std::uint32_t>(bytes[2]) << 16U)
                | (static_cast<std::uint32_t>(bytes[3]) << 24U);
            peak = std::max(peak, std::abs(std::bit_cast<float>(bits)));
        }
        const auto bin = std::min<std::size_t>(peakCount - 1,
            static_cast<std::size_t>(frame / framesPerPeak));
        peaks[bin] = std::max(peaks[bin], peak);
    }
    return peaks;
}

application::Result<std::pair<WavInfo, std::vector<float>>> WavFile::readFloatInterleaved(
    const std::filesystem::path& path) {
    const auto inspected = inspect(path);
    if (std::holds_alternative<Error>(inspected)) return std::get<Error>(inspected);
    const auto info = std::get<WavInfo>(inspected);
    if (!info.floatingPoint || info.bitsPerSample != 32 || info.frames < 0) {
        return Error{ErrorCode::invalidArgument,
            "audio clip playback requires canonical 32-bit float WAV"};
    }
    const auto sampleCount = static_cast<std::size_t>(info.frames)
        * static_cast<std::size_t>(info.channels);
    std::vector<float> samples(sampleCount, 0.0F);
    std::ifstream input{path, std::ios::binary};
    input.seekg(44, std::ios::beg);
    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        std::array<std::uint8_t, 4> bytes{};
        if (!input.read(reinterpret_cast<char*>(bytes.data()), 4)) {
            return Error{ErrorCode::recoveryFailure,
                "audio clip source ended before all samples were read"};
        }
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8U)
            | (static_cast<std::uint32_t>(bytes[2]) << 16U)
            | (static_cast<std::uint32_t>(bytes[3]) << 24U);
        samples[sample] = std::bit_cast<float>(bits);
    }
    return std::pair<WavInfo, std::vector<float>>{info, std::move(samples)};
}

}  // namespace composer::recording
