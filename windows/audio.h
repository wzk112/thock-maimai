#pragma once
#include "renderer.h"
#include <xaudio2.h>
#include <cstring>
struct Wave { WAVEFORMATEX format{};std::vector<uint8_t> pcm; };
inline Wave readWave(const fs::path& path){
    auto raw=readText(path,16*1024*1024);auto* b=reinterpret_cast<const uint8_t*>(raw.data());
    if(raw.size()<12||std::memcmp(b,"RIFF",4)||std::memcmp(b+8,"WAVE",4))throw std::runtime_error("Invalid WAV header");
    Wave w;bool gotFormat=false,gotData=false;
    for(size_t i=12;i+8<=raw.size();){uint32_t n=0;std::memcpy(&n,b+i+4,4);if(n>raw.size()-i-8)throw std::runtime_error("Truncated WAV");
        if(!std::memcmp(b+i,"fmt ",4)){if(n<16)throw std::runtime_error("Short WAV format");std::memcpy(&w.format,b+i+8,16);w.format.cbSize=0;gotFormat=true;}
        if(!std::memcmp(b+i,"data",4)){w.pcm.assign(b+i+8,b+i+8+n);gotData=true;}
        i+=8+size_t(n)+(n&1);
    }
    if(!gotFormat||!gotData||w.pcm.empty()||w.format.wFormatTag!=WAVE_FORMAT_PCM||w.format.wBitsPerSample!=16||
        (w.format.nChannels!=1&&w.format.nChannels!=2)||w.format.nSamplesPerSec<8000||w.format.nSamplesPerSec>192000||
        w.format.nBlockAlign!=w.format.nChannels*2||w.format.nAvgBytesPerSec!=w.format.nSamplesPerSec*w.format.nBlockAlign||
        w.pcm.size()%w.format.nBlockAlign)throw std::runtime_error("Use PCM16 mono/stereo WAV files");
    return w;
}
class Audio {
    Com<IXAudio2> engine;IXAudio2MasteringVoice* master=nullptr;
    struct Playing {IXAudio2SourceVoice* voice=nullptr;};std::vector<Playing> voices;
public:
    std::array<Wave,6> waves;bool available=false;
    explicit Audio(const fs::path& folder){for(size_t i=0;i<waves.size();++i)waves[i]=readWave(folder/soundFiles[i]);initialize();}
    void initialize(){stop();if(master){master->DestroyVoice();master=nullptr;}engine.Reset();available=false;
        if(FAILED(XAudio2Create(&engine)))return;
        available=SUCCEEDED(engine->CreateMasteringVoice(&master));
    }
    ~Audio(){stop();if(master)master->DestroyVoice();}
    void reap(){voices.erase(std::remove_if(voices.begin(),voices.end(),[](auto& p){XAUDIO2_VOICE_STATE s{};p.voice->GetState(&s,XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if(!s.BuffersQueued){p.voice->DestroyVoice();return true;}return false;}),voices.end());}
    void stop(){for(auto& p:voices)p.voice->DestroyVoice();voices.clear();}
    bool play(Sound sound,float volume){
        if(!available||volume<=0)return false;reap();
        if(voices.size()>=32){voices.front().voice->DestroyVoice();voices.erase(voices.begin());}
        auto& w=waves[size_t(sound)];IXAudio2SourceVoice* voice=nullptr;
        if(FAILED(engine->CreateSourceVoice(&voice,&w.format)))return false;
        voice->SetVolume(volume);XAUDIO2_BUFFER b{};b.Flags=XAUDIO2_END_OF_STREAM;b.AudioBytes=UINT32(w.pcm.size());b.pAudioData=w.pcm.data();
        if(FAILED(voice->SubmitSourceBuffer(&b))||FAILED(voice->Start())){voice->DestroyVoice();return false;}
        voices.push_back({voice});return true;
    }
};
