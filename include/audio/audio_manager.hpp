#pragma once

#include <3ds.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace PokeSurvivor::Audio {

// Audio Channel Architecture for Nintendo 3DS NDSP
constexpr int BGM_CHANNEL          = 0;
constexpr int SFX_CHANNEL_MIN      = 1;
constexpr int SFX_CHANNEL_MAX      = 15;
constexpr int SFX_CHANNEL_COUNT    = 15; // 15 dedicated hardware mixing channels

// BGM Quad-Buffering Parameters (64 KB total linear RAM footprint)
constexpr size_t BGM_BUFFER_COUNT  = 4;
constexpr size_t BGM_BUFFER_SIZE   = 16384; // 16 KB per buffer (~128 ms of 32kHz 16-bit stereo)

// Predefined Sound Effect Identifiers
enum class SoundID : uint8_t {
    HIT,
    COIN,
    XP,
    LEVELUP,
    FAINT,
    BOSS_DOWN,
    SHINY,
    PAUSE,
    BIOME_CHANGE,
    CONFIRM,
    COUNT
};

/**
 * Pre-loaded Sound Effect loaded in DSP-accessible Linear Memory.
 */
struct SoundClip {
    u8* pcmData         = nullptr;
    uint32_t dataSize   = 0;
    uint32_t sampleRate = 32000;
    uint16_t channels   = 1; // 1 = Mono, 2 = Stereo
    uint32_t nsamples   = 0;
    u16 format          = NDSP_FORMAT_MONO_PCM16;
    bool isLoaded       = false;
};

/**
 * Parsed RIFF/WAVE Container Header
 */
struct WavInfo {
    uint32_t sampleRate = 0;
    uint16_t channels   = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize   = 0;
    u16 ndspFormat      = 0;
    bool valid          = false;
};

/**
 * Audio Manager handling BGM Quad-Buffer Streaming and SFX Round-Robin Pooling.
 */
class AudioManager {
public:
    static AudioManager& Instance() {
        static AudioManager instance;
        return instance;
    }

    bool Init();
    void Shutdown();

    // BGM Streaming API
    bool PlayBGM(const char* romfsWavPath, bool loop = true);
    void StopBGM();
    void SetBGMVolume(float volume);

    // SFX Passive Playback API (Linear Memory + Cache Flush)
    bool LoadSFX(SoundID id, const char* romfsWavPath);
    void PlaySFX(SoundID id, float volume = 1.0f, float pan = 0.0f);
    void SetMasterSFXVolume(float volume);

    // Background streaming thread worker
    void StreamTick();

private:
    AudioManager() = default;
    ~AudioManager() { Shutdown(); }

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Helper: Parse standard 16-bit PCM WAV
    static WavInfo ParseWav(FILE* fp);

    // BGM Streaming State
    FILE* m_bgmFile = nullptr;
    WavInfo m_bgmInfo;
    bool m_bgmPlaying = false;
    bool m_bgmLoop = true;
    float m_bgmVolume = 0.7f;
    uint32_t m_bgmBytesRemaining = 0;

    // BGM Linear Memory Ring-Buffers
    u8* m_bgmLinearBuffers[BGM_BUFFER_COUNT] = {nullptr};
    ndspWaveBuf m_bgmWaveBuf[BGM_BUFFER_COUNT];
    size_t m_currentBgmBuf = 0;

    // BGM Thread
    Thread m_streamThread = nullptr;
    volatile bool m_threadRunning = false;

    // SFX Pool State
    SoundClip m_sfxClips[static_cast<size_t>(SoundID::COUNT)];
    ndspWaveBuf m_sfxWaveBuf[SFX_CHANNEL_COUNT];
    int m_nextSfxChannel = SFX_CHANNEL_MIN;
    float m_sfxMasterVolume = 0.85f;
    bool m_initialized = false;
};

} // namespace PokeSurvivor::Audio
