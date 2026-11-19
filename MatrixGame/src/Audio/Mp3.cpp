// MatrixGame - licensed under GPLv2 or any later version.
#include "Music.hpp"

#include <dr_mp3.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Audio {
namespace {

// An MP3 track held in memory, decoded by dr_mp3 (MPEG-1/2/2.5 Layer III; ID3, APE and other tags skipped).
class Mp3Decoder final : public Decoder {
    std::vector<uint8_t> bytes_;  // dr_mp3 reads them in place while the track plays
    drmp3 mp3_{};
    uint16_t channels_{};
    uint32_t rate_{};

public:
    explicit Mp3Decoder(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {
        if (bytes_.empty() || !drmp3_init_memory(&mp3_, bytes_.data(), bytes_.size(), nullptr))
            throw std::runtime_error("Unsupported music track; expected Ogg Vorbis or MP3 data");
        if (mp3_.channels < 1 || mp3_.channels > 2 || mp3_.sampleRate == 0) {
            drmp3_uninit(&mp3_);
            throw std::runtime_error("Unsupported music format; expected mono or stereo MP3");
        }
        channels_ = static_cast<uint16_t>(mp3_.channels);
        rate_ = mp3_.sampleRate;
    }
    ~Mp3Decoder() override { drmp3_uninit(&mp3_); }
    Mp3Decoder(const Mp3Decoder &) = delete;
    Mp3Decoder &operator=(const Mp3Decoder &) = delete;

    uint16_t channels() const override { return channels_; }
    uint32_t sample_rate() const override { return rate_; }

    size_t read(std::span<int16_t> out) override {
        const size_t frames = out.size() / channels_;
        if (frames == 0) return 0;
        // (dr_mp3 keeps the first frame's channel count and rate for the whole track)
        const auto read = drmp3_read_pcm_frames_s16(&mp3_, frames, out.data());
        return static_cast<size_t>(read) * channels_;
    }
};

}  // namespace

std::unique_ptr<Decoder> open_mp3(std::vector<uint8_t> bytes) { return std::make_unique<Mp3Decoder>(std::move(bytes)); }

std::unique_ptr<Decoder> open_track(std::vector<uint8_t> bytes) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "OggS", 4) == 0) return open_vorbis(std::move(bytes));
    return open_mp3(std::move(bytes));
}

}  // namespace Audio
