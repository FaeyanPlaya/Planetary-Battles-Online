// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Audio.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace Audio {

// Decodes one compressed track into interleaved signed 16-bit PCM.
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual uint16_t channels() const = 0;
    virtual uint32_t sample_rate() const = 0;
    // Writes whole frames from the start of out and returns the number of samples, or 0 at the end.
    virtual size_t read(std::span<int16_t> out) = 0;
};

// Opens an Ogg Vorbis track held in memory. Throws std::runtime_error for other or invalid data.
std::unique_ptr<Decoder> open_vorbis(std::vector<uint8_t> bytes);

using DecoderFactory = std::function<std::unique_ptr<Decoder>(std::vector<uint8_t>)>;

// Plays a list of tracks in order, looping the list, through one stream voice.
// Each update decodes only enough audio to keep a short queue ahead of playback.
class Music {
public:
    static constexpr int buffer_ms = 250;
    static constexpr size_t queue_target = 4;

    // The device must outlive the player. first selects the starting track.
    Music(Device &device, std::vector<std::wstring> tracks, size_t first, Loader loader, DecoderFactory open,
          Reporter report);
    Music(const Music &) = delete;
    Music &operator=(const Music &) = delete;
    ~Music();

    // Starts playback on the first call and keeps the queue filled afterwards.
    void update();
    // Listener setting and engine fade level, both 0..1; the voice plays at their product.
    void volume(float value);
    float volume() const { return volume_; }
    void level(float value);
    float level() const { return level_; }
    // False once every track has failed or the device has rejected the stream.
    bool active() const { return !disabled_; }
    // Track a later session should start with: the one after the track that was playing.
    size_t next_track() const;

private:
    Device &device_;
    std::vector<std::wstring> tracks_;
    Loader loader_;
    DecoderFactory open_;
    Reporter report_;
    std::set<size_t> failed_;
    size_t index_{};
    // Reverse member destruction releases the stream before the decoder.
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<Stream> stream_;
    uint16_t stream_channels_{};
    uint32_t stream_rate_{};
    float volume_{1.0f};
    float level_{1.0f};
    bool decoded_{};  // The current track has produced audio.
    bool started_{};  // The current stream voice is playing.
    bool played_{};   // Some audio has been queued in this session.
    bool disabled_{};

    bool open_next();
    void fail_track(const std::string &message);
    void finish_track();
    void apply_volume();
    void disable(const std::string &message);
};

} // namespace Audio
