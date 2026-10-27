// MatrixGame - licensed under GPLv2 or any later version.
#include "Music.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Audio {
namespace {
float normalized(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
} // namespace

Music::Music(Device &device, std::vector<std::wstring> tracks, size_t first, Loader loader, DecoderFactory open,
             Reporter report)
    : device_(device), tracks_(std::move(tracks)), loader_(std::move(loader)), open_(std::move(open)),
      report_(std::move(report)) {
    if (!loader_ || !open_) throw std::invalid_argument("Music requires a loader and decoder");
    disabled_ = tracks_.empty();
    if (!tracks_.empty()) index_ = first % tracks_.size();
}

Music::~Music() = default;

size_t Music::next_track() const {
    if (tracks_.empty()) return 0;
    return played_ ? (index_ + 1) % tracks_.size() : index_;
}

bool Music::open_next() {
    for (size_t attempt = 0; attempt < tracks_.size(); ++attempt) {
        if (!failed_.contains(index_)) {
            try {
                auto decoder = open_(loader_(tracks_[index_]));
                if (!decoder) throw std::runtime_error("Music decoder did not open the track");
                const auto channels = decoder->channels();
                const auto rate = decoder->sample_rate();
                if ((channels != 1 && channels != 2) || rate < 1000 || rate > 200000)
                    throw std::runtime_error("Unsupported music format; expected mono or stereo audio");
                decoder_ = std::move(decoder);
                decoded_ = false;
                return true;
            } catch (const std::exception &error) {
                failed_.insert(index_);
                if (report_) report_(tracks_[index_], error.what());
            }
        }
        index_ = (index_ + 1) % tracks_.size();
    }
    // Every track has failed; the remaining queue plays out and nothing more is decoded.
    disabled_ = true;
    return false;
}

void Music::fail_track(const std::string &message) {
    failed_.insert(index_);
    if (report_) report_(tracks_[index_], message);
    finish_track();
}

void Music::finish_track() {
    decoder_.reset();
    index_ = (index_ + 1) % tracks_.size();
}

void Music::disable(const std::string &message) {
    stream_.reset();
    decoder_.reset();
    disabled_ = true;
    if (report_) report_(L"music", message);
}

void Music::apply_volume() {
    if (stream_) stream_->volume(volume_ * level_);
}

void Music::volume(float value) {
    volume_ = normalized(value);
    try {
        apply_volume();
    } catch (const std::exception &error) { disable(error.what()); }
}

void Music::level(float value) {
    level_ = normalized(value);
    try {
        apply_volume();
    } catch (const std::exception &error) { disable(error.what()); }
}

void Music::update() {
    try {
        // Bound the work of one frame even if the voice drains short buffers while they are decoded.
        for (size_t submitted = 0;
             submitted < queue_target && !disabled_ && (!stream_ || stream_->queued() < queue_target);) {
            if (!decoder_ && !open_next()) return;
            if (stream_ && (stream_channels_ != decoder_->channels() || stream_rate_ != decoder_->sample_rate())) {
                // Let the previous track play out before replacing the voice for another format.
                if (stream_->queued() != 0) return;
                stream_.reset();
            }
            if (!stream_) {
                stream_ = device_.stream(decoder_->channels(), decoder_->sample_rate());
                if (!stream_) throw std::runtime_error("Audio device did not create a music stream");
                stream_channels_ = decoder_->channels();
                stream_rate_ = decoder_->sample_rate();
                started_ = false;
                apply_volume();
            }
            std::vector<int16_t> samples(size_t(stream_rate_) * buffer_ms / 1000 * stream_channels_);
            size_t count = 0;
            try {
                count = decoder_->read(samples);
            } catch (const std::exception &error) {
                fail_track(error.what());
                continue;
            }
            if (count > samples.size() || count % stream_channels_ != 0) {
                fail_track("Music decoder returned an incomplete frame");
                continue;
            }
            if (count == 0) {
                if (decoded_) finish_track();
                else fail_track("Music track contains no audio");
                continue;
            }
            decoded_ = true;
            played_ = true;
            samples.resize(count);
            stream_->submit(std::move(samples));
            ++submitted;
            if (!started_) {
                stream_->play();
                started_ = true;
            }
        }
    } catch (const std::exception &error) { disable(error.what()); }
}

} // namespace Audio
