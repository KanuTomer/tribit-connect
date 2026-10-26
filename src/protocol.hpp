#pragma once
// Tribit XSound Plus 2 RFCOMM framing and payloads. See PROTOCOL.md for the capture this is based on.
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace tribit {
using Bytes=std::vector<uint8_t>;
inline constexpr std::array<uint8_t,5> kHostHeader{0x08,0xEE,0x00,0x00,0x00};
inline constexpr std::array<uint8_t,5> kSpeakerHeader{0x09,0xFF,0x00,0x00,0x01};
inline constexpr size_t kOverhead=10; // header(5) 01 cmd len_lo len_hi ... sum
inline constexpr uint8_t kRfcommChannel=3;

enum Command:uint8_t{
    GetBattery=0x02,GetEq=0x05,GetDeviceInfo=0x06,GetCountdown=0x08,GetStatus09=0x09,GetAutoShutdown=0x11,
    SetEqButtons=0x18,GetEqButtons=0x19,SetEq=0x82,SetAutoShutdown=0x8C,SetVoicePrompts=0x90,GetVoicePrompts=0x91};

// Ids confirmed on the speaker on 2026-10-25.
enum Preset:uint8_t{XBassOff=0,XBass=1,Audiobook=2,MaxSound=3,Rock=4,Jazz=5,Custom=0xFE};
inline std::string preset_name(uint8_t id){
    switch(id){
        case XBassOff:return "XBass Off";case XBass:return "XBass";case MaxSound:return "MaxSound traditional";case Jazz:return "Jazz";
        case Audiobook:return "Audiobook";case Rock:return "Rock";case Custom:return "Custom";
        default:return "Unknown";
    }
}
inline constexpr size_t kEqBands=9;
inline constexpr int kBandZero=8; // band byte 8 = 0 dB, 0x0B = +3 dB
inline int band_db(uint8_t value){return static_cast<int>(value)-kBandZero;}

struct Frame{uint8_t command{};Bytes data;};

inline uint8_t checksum(std::span<const uint8_t> bytes){
    unsigned sum=0;for(auto b:bytes)sum+=b;return static_cast<uint8_t>(sum);
}
inline Bytes encode(uint8_t command,std::span<const uint8_t> data={},bool from_speaker=false){
    const size_t length=kOverhead+data.size();
    if(length>0xFFFF)throw std::invalid_argument("Frame payload is too long.");
    const auto& header=from_speaker?kSpeakerHeader:kHostHeader;
    Bytes out(header.begin(),header.end());
    out.insert(out.end(),{0x01,command,static_cast<uint8_t>(length&0xFF),static_cast<uint8_t>(length>>8)});
    out.insert(out.end(),data.begin(),data.end());
    out.push_back(checksum(out));
    return out;
}

// Reassembles speaker frames from an RFCOMM byte stream; skips noise and frames with a bad checksum.
class Parser{
public:
    void feed(std::span<const uint8_t> bytes){buffer_.insert(buffer_.end(),bytes.begin(),bytes.end());}
    std::optional<Frame> next(){
        for(;;){
            auto start=std::search(buffer_.begin(),buffer_.end(),kSpeakerHeader.begin(),kSpeakerHeader.end());
            if(start==buffer_.end()){
                // Keep a possible partial header at the end.
                const size_t keep=std::min(buffer_.size(),kSpeakerHeader.size()-1);
                discard(buffer_.size()-keep);return std::nullopt;
            }
            discard(static_cast<size_t>(start-buffer_.begin()));
            if(buffer_.size()<kOverhead)return std::nullopt;
            const size_t length=static_cast<size_t>(buffer_[7])|(static_cast<size_t>(buffer_[8])<<8);
            if(buffer_[5]!=0x01||length<kOverhead){++rejected_;discard(1);continue;}
            if(buffer_.size()<length)return std::nullopt;
            if(checksum({buffer_.data(),length-1})!=buffer_[length-1]){++rejected_;discard(1);continue;}
            Frame frame{buffer_[6],Bytes(buffer_.begin()+9,buffer_.begin()+length-1)};
            buffer_.erase(buffer_.begin(),buffer_.begin()+length);
            return frame;
        }
    }
    size_t rejected()const{return rejected_;}
    size_t skipped()const{return skipped_;}
private:
    void discard(size_t count){skipped_+=count;buffer_.erase(buffer_.begin(),buffer_.begin()+count);}
    Bytes buffer_;
    size_t rejected_{},skipped_{};
};

struct Eq{uint8_t preset{};std::array<uint8_t,kEqBands> bands{};};
inline Bytes eq_payload(const Eq& eq){
    Bytes out{eq.preset};
    if(eq.preset==Custom)out.insert(out.end(),eq.bands.begin(),eq.bands.end());
    else out.resize(1+kEqBands,0);
    return out;
}
inline std::optional<Eq> parse_eq(std::span<const uint8_t> data){
    if(data.size()!=1+kEqBands)return std::nullopt;
    Eq eq{data[0]};std::copy(data.begin()+1,data.end(),eq.bands.begin());return eq;
}

// Order matches the app: Power On/Off, Bluetooth Pairing, Low Battery, Maximum Volume, TWS Pairing.
inline constexpr std::array<const char*,5> kVoicePromptNames{"power","bluetooth_pairing","low_battery","maximum_volume","tws_pairing"};
using VoicePrompts=std::array<bool,5>;
inline std::optional<VoicePrompts> parse_voice_prompts(std::span<const uint8_t> data){
    if(data.size()!=5)return std::nullopt;
    VoicePrompts out{};
    for(size_t i=0;i<out.size();++i){if(data[i]>1)return std::nullopt;out[i]=data[i]==1;}
    return out;
}
inline Bytes voice_prompts_payload(const VoicePrompts& prompts){
    Bytes out;for(bool on:prompts)out.push_back(on?1:0);return out;
}

// EQ button: each light colour selects one preset.
inline constexpr std::array<const char*,3> kLightNames{"off","white","blue"};
struct ButtonSlot{uint8_t light{};uint8_t preset{};};
inline std::optional<ButtonSlot> parse_button_slot(std::span<const uint8_t> data){
    if(data.size()!=3+kEqBands||data[0]!=0x03||data[1]>=kLightNames.size())return std::nullopt;
    return ButtonSlot{data[1],data[2]};
}
// The app sends all three lights as back-to-back frames in one write. Each slot carries
// nine band bytes after the preset; the app always sends zeros there. Whether a custom
// slot (preset FE plus bands) is honoured is what tribit-probe --button-slot tests.
inline Bytes eq_slots_write(const std::array<Eq,3>& slots){
    Bytes out;
    for(uint8_t light=0;light<slots.size();++light){
        Bytes data{0x03,light,slots[light].preset};
        if(slots[light].preset==Custom)data.insert(data.end(),slots[light].bands.begin(),slots[light].bands.end());
        else data.resize(3+kEqBands,0);
        auto frame=encode(SetEqButtons,data);out.insert(out.end(),frame.begin(),frame.end());
    }
    return out;
}
inline Bytes eq_buttons_write(const std::array<uint8_t,3>& presets){
    return eq_slots_write(std::array<Eq,3>{Eq{presets[0]},Eq{presets[1]},Eq{presets[2]}});
}

inline std::string hex(std::span<const uint8_t> bytes){
    static constexpr char digits[]="0123456789ABCDEF";
    std::string out;
    for(size_t i=0;i<bytes.size();++i){if(i)out+=' ';out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}
    return out;
}
}
