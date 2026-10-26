#pragma once
// What this PC is playing through the speaker, in the speaker's nine EQ bands, for the Sound
// Lab's display. It captures the default output with WASAPI loopback, only while that output
// is the XSound Plus 2, and reports levels about 20 times a second while sound is playing.
// Nothing here talks to the speaker: music the phone sends to it is not seen.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class Spectrum {
public:
    static constexpr std::array<float,9> kBands{80,150,300,600,1200,2500,5000,9000,13000};
    using Levels=std::array<float,9>;               // 0..1 per band
    // Called on the capture thread. `listening` is false while the default output is not the speaker,
    // when nothing is known about playback.
    std::function<void(const Levels&,bool playing,bool listening)> on_frame;

    ~Spectrum(){stop();}
    void start(){if(running_.exchange(true))return;worker_=std::thread([this]{run();});}
    void stop(){running_=false;if(worker_.joinable())worker_.join();}
private:
    std::thread worker_;
    std::atomic<bool> running_{};

    static constexpr int kSize=2048,kHop=1024;
    void idle(int ms){for(int t=0;t<ms&&running_;t+=20)Sleep(20);}
    static bool is_speaker(IMMDevice* device){
        winrt::com_ptr<IPropertyStore> props;if(FAILED(device->OpenPropertyStore(STGM_READ,props.put())))return false;
        static const PROPERTYKEY friendly{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
        PROPVARIANT value;PropVariantInit(&value);bool match=false;
        if(SUCCEEDED(props->GetValue(friendly,&value))&&value.vt==VT_LPWSTR){
            std::wstring name=value.pwszVal;for(auto& c:name)c=static_cast<wchar_t>(towlower(c));
            match=name.find(L"xsound")!=std::wstring::npos||name.find(L"tribit")!=std::wstring::npos;
        }
        PropVariantClear(&value);return match;
    }
    void run(){
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while(running_){
            try{capture();}catch(...){}
            if(on_frame)on_frame({},false,false);
            idle(2000);   // no speaker output, or the device changed: look again shortly
        }
        winrt::uninit_apartment();
    }
    void capture(){
        auto enumerator=winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
        winrt::com_ptr<IMMDevice> device;winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,device.put()));
        if(!is_speaker(device.get()))return;
        LPWSTR id{};device->GetId(&id);std::wstring device_id=id?id:L"";CoTaskMemFree(id);
        winrt::com_ptr<IAudioClient> client;winrt::check_hresult(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,client.put_void()));
        WAVEFORMATEX* format{};winrt::check_hresult(client->GetMixFormat(&format));
        std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> owned(format,&CoTaskMemFree);
        const bool is_float=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT||(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if(!(is_float&&format->wBitsPerSample==32)&&format->wBitsPerSample!=16)return;
        winrt::check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,2000000,0,format,nullptr));
        winrt::com_ptr<IAudioCaptureClient> capture;winrt::check_hresult(client->GetService(__uuidof(IAudioCaptureClient),capture.put_void()));
        winrt::check_hresult(client->Start());
        if(on_frame)on_frame({},false,true);
        const int channels=format->nChannels;const float rate=static_cast<float>(format->nSamplesPerSec);
        std::vector<float> ring(kSize),ordered(kSize);int filled=0,fresh=0,write=0;
        auto last_sound=GetTickCount64(),last_check=last_sound;bool playing=false;
        while(running_){
            Sleep(15);
            // Silence renders nothing at all, so a quiet stretch means playback stopped.
            UINT32 packet=0;HRESULT hr=capture->GetNextPacketSize(&packet);if(FAILED(hr))break;
            while(packet){
                BYTE* data{};UINT32 frames{};DWORD flags{};
                if(FAILED(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr)))return;
                for(UINT32 f=0;f<frames;++f){
                    float sum=0;
                    if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT))for(int c=0;c<channels;++c)
                        sum+=is_float?reinterpret_cast<float*>(data)[f*channels+c]:reinterpret_cast<int16_t*>(data)[f*channels+c]/32768.f;
                    ring[write]=sum/channels;write=(write+1)%kSize;
                    filled=std::min(filled+1,kSize);++fresh;
                }
                capture->ReleaseBuffer(frames);
                if(FAILED(capture->GetNextPacketSize(&packet)))return;
            }
            auto now=GetTickCount64();
            if(filled==kSize&&fresh>=kHop){
                fresh=0;for(int i=0;i<kSize;++i)ordered[i]=ring[(write+i)%kSize];auto levels=analyse(ordered,rate);
                float loudest=*std::max_element(levels.begin(),levels.end());
                if(loudest>0.02f)last_sound=now;
                bool sounding=now-last_sound<1500;
                if(sounding||playing){playing=sounding;if(on_frame)on_frame(levels,playing,true);}
            }else if(playing&&now-last_sound>1500){playing=false;if(on_frame)on_frame({},false,true);}
            // Follow a change of the default output (headphones plugged in, say).
            if(now-last_check>2000){last_check=now;winrt::com_ptr<IMMDevice> current;
                if(FAILED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,current.put())))break;
                LPWSTR now_id{};current->GetId(&now_id);bool same=now_id&&device_id==now_id;CoTaskMemFree(now_id);if(!same)break;}
        }
        client->Stop();
    }
    // Hann-windowed FFT, then the peak level in each band (split halfway between band centres,
    // on a log scale), mapped from -60..-6 dBFS onto 0..1.
    static Levels analyse(const std::vector<float>& samples,float rate){
        std::vector<std::complex<float>> x(kSize);
        for(int i=0;i<kSize;++i)x[i]=samples[i]*(0.5f-0.5f*std::cos(6.2831853f*i/(kSize-1)));
        for(int i=1,j=0;i<kSize;++i){int bit=kSize>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
        for(int len=2;len<=kSize;len<<=1){
            std::complex<float> step=std::polar(1.f,-6.2831853f/len);
            for(int i=0;i<kSize;i+=len){std::complex<float> w=1;for(int k=0;k<len/2;++k){auto u=x[i+k],v=x[i+k+len/2]*w;x[i+k]=u+v;x[i+k+len/2]=u-v;w*=step;}}
        }
        Levels out{};const float bin=rate/kSize;
        for(size_t b=0;b<kBands.size();++b){
            float lo=b?std::sqrt(kBands[b-1]*kBands[b]):kBands[0]/std::sqrt(kBands[1]/kBands[0]);
            float hi=b+1<kBands.size()?std::sqrt(kBands[b]*kBands[b+1]):std::min(rate/2,kBands[b]*std::sqrt(kBands[b]/kBands[b-1]));
            float peak=0;for(int k=std::max(1,static_cast<int>(lo/bin));k<=std::min(kSize/2-1,static_cast<int>(hi/bin));++k)peak=std::max(peak,std::abs(x[k])/(kSize/4.f));
            float db=20*std::log10(peak+1e-9f);out[b]=std::clamp((db+60)/54,0.f,1.f);
        }
        return out;
    }
};
