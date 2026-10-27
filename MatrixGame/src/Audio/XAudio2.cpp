// MatrixGame - licensed under GPLv2 or any later version.
#include "Audio.hpp"

#include <windows.h>
#include <xaudio2.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <utility>

namespace Audio {
namespace {
std::string hex(HRESULT result) {
    char code[16]{};
    std::snprintf(code, sizeof(code), "0x%08lx", static_cast<unsigned long>(result));
    return code;
}

void check(HRESULT result, const char *operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed: " + hex(result));
}

struct ComScope {
    bool owned{};
    ComScope() {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (result != RPC_E_CHANGED_MODE) check(result, "Audio COM initialization");
        owned = SUCCEEDED(result);
    }
    ~ComScope() { if (owned) CoUninitialize(); }
};

struct Library {
    HMODULE module{LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Library() {
        if (!module) throw std::runtime_error("XAudio2.9 is unavailable; standalone audio requires Windows 10 or later");
    }
    ~Library() { FreeLibrary(module); }
};

struct ReleaseEngine { void operator()(IXAudio2 *value) const { if (value) value->Release(); } };
struct DestroyVoice {
    template<class T> void operator()(T *value) const { if (value) value->DestroyVoice(); }
};

class XVoice final : public Voice {
    // DestroyVoice waits until processing no longer reads the clip.
    std::shared_ptr<const Clip> clip_;
    std::unique_ptr<IXAudio2SourceVoice, DestroyVoice> voice_;
    IXAudio2MasteringVoice *master_;
    bool started_{};

public:
    XVoice(IXAudio2 *engine, IXAudio2MasteringVoice *master, std::shared_ptr<const Clip> clip, bool loop)
        : clip_(std::move(clip)), master_(master) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = clip_->channels;
        format.nSamplesPerSec = clip_->sample_rate;
        format.wBitsPerSample = clip_->bits;
        format.nBlockAlign = WORD(format.nChannels * (format.wBitsPerSample / 8));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        IXAudio2SourceVoice *source{};
        check(engine->CreateSourceVoice(&source, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                        nullptr, nullptr, nullptr), "CreateSourceVoice");
        voice_.reset(source);
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(clip_->samples.size());
        buffer.pAudioData = clip_->samples.data();
        if (loop) {
            buffer.LoopLength = buffer.AudioBytes / format.nBlockAlign;
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        }
        check(voice_->SubmitSourceBuffer(&buffer, nullptr), "SubmitSourceBuffer");
        pan(0.0f);
    }

    void play() override {
        if (!started_) {
            check(voice_->Start(0, XAUDIO2_COMMIT_NOW), "Start audio voice");
            started_ = true;
        }
    }
    bool playing() const override {
        XAUDIO2_VOICE_STATE state{};
        voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return started_ && state.BuffersQueued != 0;
    }
    void volume(float value) override { check(voice_->SetVolume(value, XAUDIO2_COMMIT_NOW), "Set audio volume"); }
    void pan(float value) override {
        const float left = value > 0.0f ? 1.0f - value : 1.0f;
        const float right = value < 0.0f ? 1.0f + value : 1.0f;
        std::array<float, 4> matrix{left, right, 0.0f, 0.0f};
        if (clip_->channels == 2) matrix = {left, 0.0f, 0.0f, right};
        check(voice_->SetOutputMatrix(master_, clip_->channels, 2, matrix.data(), XAUDIO2_COMMIT_NOW), "Set audio balance");
    }
};

class XStream final : public Stream {
    // Reverse member destruction stops the voice before its buffers are released.
    std::deque<std::vector<int16_t>> buffers_;
    std::unique_ptr<IXAudio2SourceVoice, DestroyVoice> voice_;

    UINT32 pending() const {
        XAUDIO2_VOICE_STATE state{};
        voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return state.BuffersQueued;
    }

public:
    XStream(IXAudio2 *engine, IXAudio2MasteringVoice *master, uint16_t channels, uint32_t sample_rate) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = channels;
        format.nSamplesPerSec = sample_rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = WORD(channels * 2);
        format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
        IXAudio2SourceVoice *source{};
        check(engine->CreateSourceVoice(&source, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                        nullptr, nullptr, nullptr), "CreateSourceVoice");
        voice_.reset(source);
        std::array<float, 4> matrix{1.0f, 1.0f, 0.0f, 0.0f};
        if (channels == 2) matrix = {1.0f, 0.0f, 0.0f, 1.0f};
        check(voice_->SetOutputMatrix(master, channels, 2, matrix.data(), XAUDIO2_COMMIT_NOW), "Set music balance");
    }

    void submit(std::vector<int16_t> samples) override {
        // Buffers finish in submission order, so the oldest ones beyond the queue are done.
        for (const UINT32 queued = pending(); buffers_.size() > queued;) buffers_.pop_front();
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(samples.size() * sizeof(int16_t));
        buffer.pAudioData = reinterpret_cast<const BYTE *>(samples.data());
        check(voice_->SubmitSourceBuffer(&buffer, nullptr), "Submit music buffer");
        // Only a queued buffer is kept; moving the vector keeps the storage the voice reads.
        buffers_.push_back(std::move(samples));
    }
    size_t queued() const override { return pending(); }
    void play() override { check(voice_->Start(0, XAUDIO2_COMMIT_NOW), "Start music voice"); }
    void volume(float value) override { check(voice_->SetVolume(value, XAUDIO2_COMMIT_NOW), "Set music volume"); }
};

// Records a critical engine error. XAudio2 calls it on its own thread, where the engine must not be used.
class EngineErrors final : public IXAudio2EngineCallback {
    std::atomic<HRESULT> error_{S_OK};

public:
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) override { error_.store(FAILED(error) ? error : E_FAIL); }
    HRESULT error() const { return error_.load(); }
};

class XDevice final : public Device {
    ComScope com_;
    Library library_;
    // Declared before the engine, so it outlives every callback the engine makes.
    EngineErrors errors_;
    std::unique_ptr<IXAudio2, ReleaseEngine> engine_;
    std::unique_ptr<IXAudio2MasteringVoice, DestroyVoice> master_;

public:
    XDevice() {
        using Create = HRESULT (WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
        const auto address = GetProcAddress(library_.module, "XAudio2Create");
        if (!address) throw std::runtime_error("XAudio2Create is unavailable");
        const auto create = std::bit_cast<Create>(address);
        IXAudio2 *engine{};
        check(create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create");
        engine_.reset(engine);
        check(engine_->RegisterForCallbacks(&errors_), "Register audio engine callback");
        IXAudio2MasteringVoice *master{};
        check(engine_->CreateMasteringVoice(&master, 2, XAUDIO2_DEFAULT_SAMPLERATE, 0,
                                            nullptr, nullptr, AudioCategory_GameEffects), "CreateMasteringVoice");
        master_.reset(master);
    }
    std::unique_ptr<Voice> create(std::shared_ptr<const Clip> clip, bool loop) override {
        return std::make_unique<XVoice>(engine_.get(), master_.get(), std::move(clip), loop);
    }
    std::unique_ptr<Stream> stream(uint16_t channels, uint32_t sample_rate) override {
        return std::make_unique<XStream>(engine_.get(), master_.get(), channels, sample_rate);
    }
    std::string critical_error() const override {
        const HRESULT error = errors_.error();
        return SUCCEEDED(error) ? std::string{} : "XAudio2 critical error " + hex(error);
    }
};
} // namespace

std::unique_ptr<Device> create_xaudio_device() { return std::make_unique<XDevice>(); }
} // namespace Audio
