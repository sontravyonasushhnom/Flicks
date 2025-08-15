#pragma once
#include <xaudio2.h>
#include <cstdint>

struct SoundDataXA {
    WAVEFORMATEX wfx = {};
    BYTE* pData = nullptr;
    UINT32 dataSize = 0;
};

extern IXAudio2* g_pXAudio2;
extern IXAudio2MasteringVoice* g_pMasterVoice;
extern SoundDataXA g_hitSound;
extern IXAudio2SourceVoice* g_pHitVoices[];
extern int g_nextHitVoiceIndex;
extern const int HIT_VOICE_COUNT;

bool InitXAudio2();
void CleanupXAudio2();
void PlayHitSound();