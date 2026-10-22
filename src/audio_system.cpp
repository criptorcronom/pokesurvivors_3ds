#include "audio/audio_manager.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace PokeSurvivor::Audio {

static void BgmThreadEntry(void* arg) {
    AudioManager* mgr = static_cast<AudioManager*>(arg);
    while (mgr) {
        mgr->StreamTick();
        svcSleepThread(10 * 1000 * 1000); // 10ms poll
    }
}

WavInfo AudioManager::ParseWav(FILE* fp) {
    WavInfo info{};
    if (!fp) return info;

    char header[12];
    if (fread(header, 1, 12, fp) != 12) return info;

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return info;
    }

    bool foundFmt = false;
    bool foundData = false;

    while (!feof(fp)) {
        char chunkId[4];
        uint32_t chunkSize = 0;

        if (fread(chunkId, 1, 4, fp) != 4) break;
        if (fread(&chunkSize, 4, 1, fp) != 1) break;

        long chunkDataPos = ftell(fp);

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t audioFormat = 0;
            fread(&audioFormat, 2, 1, fp);
            fread(&info.channels, 2, 1, fp);
            fread(&info.sampleRate, 4, 1, fp);

            fseek(fp, chunkDataPos + 14, SEEK_SET);
            fread(&info.bitsPerSample, 2, 1, fp);

            if (audioFormat == 1) { // PCM
                if (info.bitsPerSample == 16) {
                    info.ndspFormat = (info.channels == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
                    foundFmt = true;
                } else if (info.bitsPerSample == 8) {
                    info.ndspFormat = (info.channels == 2) ? NDSP_FORMAT_STEREO_PCM8 : NDSP_FORMAT_MONO_PCM8;
                    foundFmt = true;
                }
            }
        } else if (memcmp(chunkId, "data", 4) == 0) {
            info.dataOffset = static_cast<uint32_t>(chunkDataPos);
            info.dataSize   = chunkSize;
            foundData = true;
            break; // Data chunk found, ready to stream
        }

        fseek(fp, chunkDataPos + chunkSize, SEEK_SET);
    }

    info.valid = foundFmt && foundData && (info.dataSize > 0);
    return info;
}

bool AudioManager::Init() {
    if (m_initialized) return true;

    // 1. NDSP Subsystem Initialization
    Result res = ndspInit();
    if (R_FAILED(res)) {
        return false;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    // 2. Allocate BGM Quad-Buffers in Hardware Linear Memory
    // The DSP accesses memory via DMA and requires physical contiguous linear memory.
    for (size_t i = 0; i < BGM_BUFFER_COUNT; ++i) {
        m_bgmLinearBuffers[i] = static_cast<u8*>(linearAlloc(BGM_BUFFER_SIZE));
        if (!m_bgmLinearBuffers[i]) {
            Shutdown();
            return false;
        }
        std::memset(m_bgmLinearBuffers[i], 0, BGM_BUFFER_SIZE);

        std::memset(&m_bgmWaveBuf[i], 0, sizeof(ndspWaveBuf));
        m_bgmWaveBuf[i].data_vaddr = m_bgmLinearBuffers[i];
        m_bgmWaveBuf[i].status     = NDSP_WBUF_DONE; // Ready for feeding
    }

    // 3. Reset and prepare SFX WaveBuf structures
    for (int i = 0; i < SFX_CHANNEL_COUNT; ++i) {
        std::memset(&m_sfxWaveBuf[i], 0, sizeof(ndspWaveBuf));
        m_sfxWaveBuf[i].status = NDSP_WBUF_DONE;
        ndspChnReset(SFX_CHANNEL_MIN + i);
    }

    // 4. Spawn Background Streaming Thread
    // Priority 0x32 (slightly lower than main thread 0x30 to never starve physics)
    m_threadRunning = true;
    m_streamThread = threadCreate(BgmThreadEntry, this, 16 * 1024, 0x32, 1, false);

    m_initialized = true;
    return true;
}

void AudioManager::Shutdown() {
    // Stop and destroy streaming thread
    if (m_threadRunning) {
        m_threadRunning = false;
        if (m_streamThread) {
            threadJoin(m_streamThread, U64_MAX);
            threadFree(m_streamThread);
            m_streamThread = nullptr;
        }
    }

    StopBGM();

    // Free BGM Linear Buffers
    for (size_t i = 0; i < BGM_BUFFER_COUNT; ++i) {
        if (m_bgmLinearBuffers[i]) {
            linearFree(m_bgmLinearBuffers[i]);
            m_bgmLinearBuffers[i] = nullptr;
        }
    }

    // Free SFX Linear Buffers
    for (size_t i = 0; i < static_cast<size_t>(SoundID::COUNT); ++i) {
        if (m_sfxClips[i].pcmData) {
            linearFree(m_sfxClips[i].pcmData);
            m_sfxClips[i].pcmData = nullptr;
            m_sfxClips[i].isLoaded = false;
        }
    }

    if (m_initialized) {
        ndspExit();
        m_initialized = false;
    }
}

bool AudioManager::PlayBGM(const char* romfsWavPath, bool loop) {
    StopBGM();

    m_bgmFile = fopen(romfsWavPath, "rb");
    if (!m_bgmFile) {
        return false;
    }

    m_bgmInfo = ParseWav(m_bgmFile);
    if (!m_bgmInfo.valid) {
        fclose(m_bgmFile);
        m_bgmFile = nullptr;
        return false;
    }

    m_bgmLoop = loop;
    m_bgmBytesRemaining = m_bgmInfo.dataSize;
    fseek(m_bgmFile, m_bgmInfo.dataOffset, SEEK_SET);

    // Reset and configure BGM Channel 0
    ndspChnReset(BGM_CHANNEL);
    ndspChnSetFormat(BGM_CHANNEL, m_bgmInfo.ndspFormat);
    ndspChnSetRate(BGM_CHANNEL, static_cast<float>(m_bgmInfo.sampleRate));

    float mix[12] = {0};
    mix[0] = m_bgmVolume; // Front Left
    mix[1] = m_bgmVolume; // Front Right
    ndspChnSetMix(BGM_CHANNEL, mix);

    // Prime the initial 4 buffers
    for (size_t i = 0; i < BGM_BUFFER_COUNT; ++i) {
        m_bgmWaveBuf[i].status = NDSP_WBUF_DONE;
    }
    m_currentBgmBuf = 0;
    m_bgmPlaying = true;

    StreamTick(); // Initial feed
    return true;
}

void AudioManager::StopBGM() {
    m_bgmPlaying = false;
    ndspChnWaveBufClear(BGM_CHANNEL);
    ndspChnReset(BGM_CHANNEL);

    if (m_bgmFile) {
        fclose(m_bgmFile);
        m_bgmFile = nullptr;
    }
}

void AudioManager::SetBGMVolume(float volume) {
    m_bgmVolume = (volume < 0.0f) ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (m_bgmPlaying) {
        float mix[12] = {0};
        mix[0] = m_bgmVolume;
        mix[1] = m_bgmVolume;
        ndspChnSetMix(BGM_CHANNEL, mix);
    }
}

void AudioManager::StreamTick() {
    if (!m_bgmPlaying || !m_bgmFile) return;

    for (size_t i = 0; i < BGM_BUFFER_COUNT; ++i) {
        ndspWaveBuf& buf = m_bgmWaveBuf[i];

        if (buf.status == NDSP_WBUF_DONE || buf.status == NDSP_WBUF_FREE) {
            uint32_t toRead = BGM_BUFFER_SIZE;
            if (toRead > m_bgmBytesRemaining) {
                toRead = m_bgmBytesRemaining;
            }

            size_t bytesRead = fread(const_cast<void*>(buf.data_vaddr), 1, toRead, m_bgmFile);
            m_bgmBytesRemaining -= static_cast<uint32_t>(bytesRead);

            // Handle track looping or end of file
            if (m_bgmBytesRemaining == 0 || bytesRead == 0) {
                if (m_bgmLoop) {
                    fseek(m_bgmFile, m_bgmInfo.dataOffset, SEEK_SET);
                    m_bgmBytesRemaining = m_bgmInfo.dataSize;

                    // Fill remainder of the buffer if any space is left
                    if (bytesRead < BGM_BUFFER_SIZE) {
                        size_t wrapRead = fread(
                            static_cast<u8*>(const_cast<void*>(buf.data_vaddr)) + bytesRead,
                            1,
                            BGM_BUFFER_SIZE - bytesRead,
                            m_bgmFile
                        );
                        bytesRead += wrapRead;
                        m_bgmBytesRemaining -= static_cast<uint32_t>(wrapRead);
                    }
                } else {
                    m_bgmPlaying = false;
                }
            }

            if (bytesRead > 0) {
                // Calculate sample count for 16-bit PCM
                uint32_t bytesPerSample = (m_bgmInfo.bitsPerSample / 8) * m_bgmInfo.channels;
                buf.nsamples = static_cast<uint32_t>(bytesRead) / bytesPerSample;
                buf.status = NDSP_WBUF_FREE;
                buf.looping = false;
                buf.offset = 0;

                // CRITICAL: Flush cache lines so DSP DMA reads valid RAM data
                DSP_FlushDataCache(buf.data_vaddr, bytesRead);

                ndspChnWaveBufAdd(BGM_CHANNEL, &buf);
            }
        }
    }
}

bool AudioManager::LoadSFX(SoundID id, const char* romfsWavPath) {
    size_t idx = static_cast<size_t>(id);
    if (idx >= static_cast<size_t>(SoundID::COUNT)) return false;

    FILE* fp = fopen(romfsWavPath, "rb");
    if (!fp) return false;

    WavInfo info = ParseWav(fp);
    if (!info.valid) {
        fclose(fp);
        return false;
    }

    // Allocate in linear memory compatible with DSP DMA
    u8* linearBuffer = static_cast<u8*>(linearAlloc(info.dataSize));
    if (!linearBuffer) {
        fclose(fp);
        return false;
    }

    fseek(fp, info.dataOffset, SEEK_SET);
    size_t bytesRead = fread(linearBuffer, 1, info.dataSize, fp);
    fclose(fp);

    if (bytesRead != info.dataSize) {
        linearFree(linearBuffer);
        return false;
    }

    // Flush cache so DSP DMA has fresh data
    DSP_FlushDataCache(linearBuffer, info.dataSize);

    SoundClip& clip = m_sfxClips[idx];
    if (clip.pcmData) {
        linearFree(clip.pcmData);
    }

    clip.pcmData     = linearBuffer;
    clip.dataSize    = info.dataSize;
    clip.sampleRate  = info.sampleRate;
    clip.channels    = info.channels;
    clip.format      = info.ndspFormat;
    uint32_t bytesPerSample = (info.bitsPerSample / 8) * info.channels;
    clip.nsamples    = info.dataSize / bytesPerSample;
    clip.isLoaded    = true;

    return true;
}

void AudioManager::PlaySFX(SoundID id, float volume, float pan) {
    size_t idx = static_cast<size_t>(id);
    if (idx >= static_cast<size_t>(SoundID::COUNT)) return;

    const SoundClip& clip = m_sfxClips[idx];
    if (!clip.isLoaded || !clip.pcmData) return;

    // 15-Channel Round-Robin Pool search
    int selectedChn = -1;
    for (int i = 0; i < SFX_CHANNEL_COUNT; ++i) {
        int chn = m_nextSfxChannel;
        m_nextSfxChannel = SFX_CHANNEL_MIN + ((m_nextSfxChannel - SFX_CHANNEL_MIN + 1) % SFX_CHANNEL_COUNT);

        int bufIdx = chn - SFX_CHANNEL_MIN;
        if (!ndspChnIsPlaying(chn) || m_sfxWaveBuf[bufIdx].status == NDSP_WBUF_DONE) {
            selectedChn = chn;
            break;
        }
    }

    // If all 15 channels are busy playing explosions, drop safely to avoid bus saturation
    if (selectedChn < 0) return;

    int bufIdx = selectedChn - SFX_CHANNEL_MIN;
    ndspWaveBuf& wave = m_sfxWaveBuf[bufIdx];

    ndspChnReset(selectedChn);
    ndspChnSetFormat(selectedChn, clip.format);
    ndspChnSetRate(selectedChn, static_cast<float>(clip.sampleRate));

    // Calculate Stereo Panning & Master Volume
    float finalVol = volume * m_sfxMasterVolume;
    float panClamped = (pan < -1.0f) ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    float leftMix  = finalVol * (panClamped <= 0.0f ? 1.0f : (1.0f - panClamped));
    float rightMix = finalVol * (panClamped >= 0.0f ? 1.0f : (1.0f + panClamped));

    float mix[12] = {0};
    mix[0] = leftMix;
    mix[1] = rightMix;
    ndspChnSetMix(selectedChn, mix);

    wave.data_vaddr = clip.pcmData;
    wave.nsamples   = clip.nsamples;
    wave.status     = NDSP_WBUF_FREE;
    wave.looping    = false;
    wave.offset     = 0;

    // Ensure data cache is flushed to physical memory
    DSP_FlushDataCache(clip.pcmData, clip.dataSize);

    ndspChnWaveBufAdd(selectedChn, &wave);
}

void AudioManager::SetMasterSFXVolume(float volume) {
    m_sfxMasterVolume = (volume < 0.0f) ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

} // namespace PokeSurvivor::Audio
