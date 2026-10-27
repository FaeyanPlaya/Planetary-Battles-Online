// MatrixGame - licensed under GPLv2 or any later version.
#include "Audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace Audio {
namespace {
uint16_t u16(std::span<const uint8_t> data, size_t pos) {
    return uint16_t(data[pos]) | uint16_t(uint16_t(data[pos + 1]) << 8);
}

uint32_t u32(std::span<const uint8_t> data, size_t pos) {
    return uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) |
           (uint32_t(data[pos + 2]) << 16) | (uint32_t(data[pos + 3]) << 24);
}

bool tag(std::span<const uint8_t> data, size_t pos, const char *name) {
    return std::memcmp(data.data() + pos, name, 4) == 0;
}

float normalized(float value, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : 0.0f;
}
} // namespace

Clip read_wave(std::span<const uint8_t> bytes) {
    if (bytes.size() < 12 || !tag(bytes, 0, "RIFF") || !tag(bytes, 8, "WAVE"))
        throw std::runtime_error("Expected a RIFF/WAVE sound");
    const uint64_t end64 = uint64_t(u32(bytes, 4)) + 8;
    // Several original clips count the RIFF header in the outer size field.
    const bool legacy_size = end64 == uint64_t(bytes.size()) + 8;
    if (end64 < 12 || (end64 > bytes.size() && !legacy_size))
        throw std::runtime_error("Truncated RIFF sound");
    const size_t end = legacy_size ? bytes.size() : static_cast<size_t>(end64);
    Clip clip;
    bool have_format = false;
    bool have_data = false;
    std::span<const uint8_t> samples;
    for (size_t pos = 12; pos < end;) {
        if (end - pos < 8) throw std::runtime_error("Truncated WAV chunk header");
        const size_t length = u32(bytes, pos + 4);
        if (length > end - pos - 8) throw std::runtime_error("Truncated WAV chunk");
        const size_t payload = pos + 8;
        if (tag(bytes, pos, "fmt ")) {
            if (have_format || length < 16) throw std::runtime_error("Invalid WAV format chunk");
            have_format = true;
            clip.channels = u16(bytes, payload + 2);
            clip.sample_rate = u32(bytes, payload + 4);
            clip.bits = u16(bytes, payload + 14);
            if (u16(bytes, payload) != 1 || (clip.channels != 1 && clip.channels != 2) ||
                (clip.bits != 8 && clip.bits != 16) || clip.sample_rate < 1000 || clip.sample_rate > 200000)
                throw std::runtime_error("Unsupported sound format; use mono/stereo PCM8/16 WAV");
            const uint16_t frame = uint16_t(clip.channels * (clip.bits / 8));
            if (u16(bytes, payload + 12) != frame || u32(bytes, payload + 8) != clip.sample_rate * frame)
                throw std::runtime_error("Inconsistent WAV frame format");
        } else if (tag(bytes, pos, "data")) {
            if (have_data) throw std::runtime_error("Duplicate WAV sample chunk");
            have_data = true;
            samples = bytes.subspan(payload, length);
        }
        size_t padding = length & 1;
        // Original PCM8 clips can omit the pad on their final sample chunk.
        if (padding && tag(bytes, pos, "data") && payload + length == end) padding = 0;
        if (padding > end - payload - length) throw std::runtime_error("Missing WAV chunk padding");
        pos = payload + length + padding;
    }
    if (!have_format || !have_data || samples.empty() ||
        samples.size() % (clip.channels * (clip.bits / 8)) != 0)
        throw std::runtime_error("Missing or incomplete WAV samples");
    clip.samples.assign(samples.begin(), samples.end());
    return clip;
}

Service::Service(std::unique_ptr<Device> device, Mapping mapping, Loader loader, Reporter report, uint64_t first_id)
    : device_(std::move(device)), mapping_(std::move(mapping)), loader_(std::move(loader)), report_(std::move(report)),
      next_id_(std::max<uint64_t>(first_id, 1)) {
    if (!device_ || !loader_) throw std::invalid_argument("Audio service requires a device and loader");
}

void Service::failure(const std::wstring &name, const std::string &message) {
    if (failures_.insert(name).second && report_) report_(name, message);
}

void Service::voice_failure(uint32_t id, const std::string &message) {
    const auto found = voices_.find(id);
    if (found != voices_.end()) {
        failure(found->second.name, message);
        voices_.erase(found);
    }
}

uint32_t Service::create(const std::wstring &name, bool loop) {
    if (failures_.contains(name)) return invalid_id;
    const auto definition = mapping_.find(name);
    if (definition == mapping_.end()) {
        failure(name, "Sound name has no resource mapping");
        return invalid_id;
    }
    if (next_id_ >= invalid_id) {
        failure(name, "Audio handle space exhausted");
        return invalid_id;
    }
    try {
        auto cached = clips_.find(definition->second);
        if (cached == clips_.end()) {
            auto bytes = loader_(definition->second);
            auto clip = std::make_shared<Clip>(read_wave(bytes));
            cached = clips_.emplace(definition->second, std::move(clip)).first;
        }
        auto voice = device_->create(cached->second, loop);
        if (!voice) throw std::runtime_error("Audio device did not create a voice");
        const uint32_t id = static_cast<uint32_t>(next_id_++);
        if (master_ != 1.0f) voice->volume(master_);
        voices_.emplace(id, Entry{std::move(voice), name, 1.0f, 0.0f});
        return id;
    } catch (const std::exception &error) {
        failure(name, error.what());
        return invalid_id;
    }
}

void Service::destroy(uint32_t id) { voices_.erase(id); }
void Service::play(uint32_t id) {
    try {
        if (auto found = voices_.find(id); found != voices_.end()) found->second.voice->play();
    } catch (const std::exception &error) { voice_failure(id, error.what()); }
}
bool Service::playing(uint32_t id) {
    try {
        const auto found = voices_.find(id);
        return found != voices_.end() && found->second.voice->playing();
    } catch (const std::exception &error) { voice_failure(id, error.what()); }
    return false;
}
void Service::volume(uint32_t id, float value) {
    try {
        if (auto found = voices_.find(id); found != voices_.end()) {
            value = normalized(value, 0.0f, 1.0f);
            found->second.voice->volume(value * master_);
            found->second.volume = value;
        }
    } catch (const std::exception &error) { voice_failure(id, error.what()); }
}
void Service::pan(uint32_t id, float value) {
    try {
        if (auto found = voices_.find(id); found != voices_.end()) {
            value = normalized(value, -1.0f, 1.0f);
            found->second.voice->pan(value);
            found->second.pan = value;
        }
    } catch (const std::exception &error) { voice_failure(id, error.what()); }
}
void Service::master_volume(float value) {
    master_ = normalized(value, 0.0f, 1.0f);
    for (auto it = voices_.begin(); it != voices_.end();) {
        const auto id = it->first;
        auto &entry = it->second;
        ++it;  // A failing voice is erased by voice_failure.
        try {
            entry.voice->volume(entry.volume * master_);
        } catch (const std::exception &error) { voice_failure(id, error.what()); }
    }
}
float Service::volume(uint32_t id) const {
    const auto found = voices_.find(id);
    return found == voices_.end() ? 0.0f : found->second.volume;
}
float Service::pan(uint32_t id) const {
    const auto found = voices_.find(id);
    return found == voices_.end() ? 0.0f : found->second.pan;
}
void Service::clear() {
    voices_.clear();
    clips_.clear();
    failures_.clear();
    // Do not recycle IDs: stale unit/effect handles cannot alias a later sound.
}
} // namespace Audio
