#include "audio_xa.h"
#include <fstream>
#include <cstring>
#include <cstdlib>

// Global audio variables
IXAudio2* g_pXAudio2 = nullptr;
IXAudio2MasteringVoice* g_pMasterVoice = nullptr;
SoundDataXA g_hitSound;
const int HIT_VOICE_COUNT = 4;
IXAudio2SourceVoice* g_pHitVoices[HIT_VOICE_COUNT] = { nullptr };
int g_nextHitVoiceIndex = 0;

namespace {
    bool LoadWAVFromFile(const wchar_t* filename, SoundDataXA& outSound) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;

        auto cleanup = [&] {
            if (outSound.pData) free(outSound.pData);
            outSound = SoundDataXA{};
            return false;
            };

        char chunkId[4];
        uint32_t chunkSize;
        char format[4];
        file.read(chunkId, 4);
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        file.read(format, 4);
        if (std::memcmp(chunkId, "RIFF", 4) != 0 || std::memcmp(format, "WAVE", 4) != 0)
            return cleanup();

        bool fmtFound = false, dataFound = false;
        WAVEFORMATEX wfx = {};
        BYTE* pData = nullptr;
        uint32_t dataSize = 0;

        while (file.read(chunkId, 4)) {
            file.read(reinterpret_cast<char*>(&chunkSize), 4);
            uint32_t chunkDataPos = static_cast<uint32_t>(file.tellg());

            if (std::memcmp(chunkId, "fmt ", 4) == 0) {
                if (chunkSize < 16) return cleanup();

                WORD audioFormat, numChannels, blockAlign, bitsPerSample;
                DWORD sampleRate, byteRate;
                file.read(reinterpret_cast<char*>(&audioFormat), sizeof(audioFormat));
                file.read(reinterpret_cast<char*>(&numChannels), sizeof(numChannels));
                file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
                file.read(reinterpret_cast<char*>(&byteRate), sizeof(byteRate));
                file.read(reinterpret_cast<char*>(&blockAlign), sizeof(blockAlign));
                file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));

                if (audioFormat != WAVE_FORMAT_PCM) return cleanup();
                wfx = { audioFormat, numChannels, sampleRate, byteRate, blockAlign, bitsPerSample, 0 };
                fmtFound = true;
            }
            else if (std::memcmp(chunkId, "data", 4) == 0) {
                dataSize = chunkSize;
                pData = static_cast<BYTE*>(malloc(dataSize));
                if (!pData || !file.read(reinterpret_cast<char*>(pData), dataSize))
                    return cleanup();
                dataFound = true;
            }
            file.seekg(chunkDataPos + chunkSize, std::ios::beg);
        }

        if (!fmtFound || !dataFound) return cleanup();

        outSound.wfx = wfx;
        outSound.pData = pData;
        outSound.dataSize = dataSize;
        return true;
    }

    void UnloadSoundData(SoundDataXA& sound) {
        if (sound.pData) free(sound.pData);
        sound = SoundDataXA{};
    }
}

bool InitXAudio2() {
    if (FAILED(XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
        return false;

    if (FAILED(g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice))) {
        g_pXAudio2->Release();
        g_pXAudio2 = nullptr;
        return false;
    }

    if (LoadWAVFromFile(L"res/hit.wav", g_hitSound)) {
        for (int i = 0; i < HIT_VOICE_COUNT; ++i) {
            g_pXAudio2->CreateSourceVoice(&g_pHitVoices[i], &g_hitSound.wfx);
        }
    }
    return true;
}

void CleanupXAudio2() {
    for (int i = 0; i < HIT_VOICE_COUNT; ++i) {
        if (g_pHitVoices[i]) {
            g_pHitVoices[i]->DestroyVoice();
            g_pHitVoices[i] = nullptr;
        }
    }
    UnloadSoundData(g_hitSound);

    if (g_pMasterVoice) {
        g_pMasterVoice->DestroyVoice();
        g_pMasterVoice = nullptr;
    }
    if (g_pXAudio2) {
        g_pXAudio2->Release();
        g_pXAudio2 = nullptr;
    }
}

void PlayHitSound() {
    if (!g_hitSound.pData) return;

    IXAudio2SourceVoice* voice = g_pHitVoices[g_nextHitVoiceIndex];
    if (!voice) return;

    voice->Stop();
    voice->FlushSourceBuffers();

    XAUDIO2_BUFFER buffer = {};
    buffer.pAudioData = g_hitSound.pData;
    buffer.AudioBytes = g_hitSound.dataSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) {
        voice->Start();
    }
    g_nextHitVoiceIndex = (g_nextHitVoiceIndex + 1) % HIT_VOICE_COUNT;
}