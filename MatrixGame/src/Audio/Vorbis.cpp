// MatrixGame - licensed under GPLv2 or any later version.
#include "Music.hpp"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace Audio {
namespace {

// Compressed track bytes read through libvorbisfile's callback interface.
struct Source {
    std::vector<uint8_t> bytes;
    size_t position{};
};

size_t read_source(void *out, size_t size, size_t count, void *user) {
    auto &source = *static_cast<Source *>(user);
    if (size == 0) return 0;
    const size_t items = std::min(count, (source.bytes.size() - source.position) / size);
    std::memcpy(out, source.bytes.data() + source.position, items * size);
    source.position += items * size;
    return items;
}

int seek_source(void *user, ogg_int64_t offset, int origin) {
    auto &source = *static_cast<Source *>(user);
    ogg_int64_t base = 0;
    if (origin == SEEK_CUR) base = static_cast<ogg_int64_t>(source.position);
    else if (origin == SEEK_END) base = static_cast<ogg_int64_t>(source.bytes.size());
    else if (origin != SEEK_SET) return -1;
    const ogg_int64_t target = base + offset;
    if (target < 0 || target > static_cast<ogg_int64_t>(source.bytes.size())) return -1;
    source.position = static_cast<size_t>(target);
    return 0;
}

long tell_source(void *user) { return static_cast<long>(static_cast<Source *>(user)->position); }

class VorbisDecoder final : public Decoder {
    Source source_;
    OggVorbis_File file_{};
    uint16_t channels_{};
    uint32_t rate_{};

public:
    explicit VorbisDecoder(std::vector<uint8_t> bytes) : source_{std::move(bytes)} {
        if (source_.bytes.size() < 4 || std::memcmp(source_.bytes.data(), "OggS", 4) != 0)
            throw std::runtime_error("Unsupported music track; expected Ogg Vorbis data");
        if (source_.bytes.size() > size_t(std::numeric_limits<long>::max()))
            throw std::runtime_error("Music track is too large");
        const ov_callbacks callbacks{read_source, seek_source, nullptr, tell_source};
        if (const int result = ov_open_callbacks(&source_, &file_, nullptr, 0, callbacks); result != 0)
            throw std::runtime_error("Invalid Ogg Vorbis music track (" + std::to_string(result) + ")");
        const vorbis_info *info = ov_info(&file_, -1);
        if (!info || info->channels < 1 || info->channels > 2 || info->rate <= 0) {
            ov_clear(&file_);
            throw std::runtime_error("Unsupported music format; expected mono or stereo Ogg Vorbis");
        }
        channels_ = static_cast<uint16_t>(info->channels);
        rate_ = static_cast<uint32_t>(info->rate);
    }
    ~VorbisDecoder() override { ov_clear(&file_); }
    VorbisDecoder(const VorbisDecoder &) = delete;
    VorbisDecoder &operator=(const VorbisDecoder &) = delete;

    uint16_t channels() const override { return channels_; }
    uint32_t sample_rate() const override { return rate_; }

    size_t read(std::span<int16_t> out) override {
        const size_t frame = channels_;
        const size_t limit = out.size() / frame * frame;
        size_t written = 0;
        while (written < limit) {
            int section = 0;
            const int bytes = static_cast<int>(std::min<size_t>((limit - written) * sizeof(int16_t), 1u << 20));
            const long result = ov_read(&file_, reinterpret_cast<char *>(out.data() + written), bytes, 0, 2, 1,
                                        &section);
            if (result == 0) break;
            // A hole is a recoverable gap in the stream; other errors end this track.
            if (result == OV_HOLE) continue;
            if (result < 0) throw std::runtime_error("Corrupt Ogg Vorbis music data (" + std::to_string(result) + ")");
            // A chained stream with another layout cannot share the voice.
            const vorbis_info *info = ov_info(&file_, section);
            if (!info || info->channels != channels_ || static_cast<uint32_t>(info->rate) != rate_)
                throw std::runtime_error("Music track changes its audio format");
            written += static_cast<size_t>(result) / sizeof(int16_t);
        }
        return written - written % frame;
    }
};

} // namespace

std::unique_ptr<Decoder> open_vorbis(std::vector<uint8_t> bytes) {
    return std::make_unique<VorbisDecoder>(std::move(bytes));
}

} // namespace Audio
