#include "speaker.hpp"
#include <iostream>
#include <thread>

using namespace tribit;
using namespace std::chrono_literals;

static std::string quoted(const std::string& text){
    std::string out="\"";
    for(char c:text){
        if(c=='"'||c=='\\'){out+='\\';out+=c;}
        else if(static_cast<unsigned char>(c)<0x20){char esc[8];std::snprintf(esc,sizeof esc,"\\u%04x",c);out+=esc;}
        else out+=c;
    }
    return out+"\"";
}
static std::string command_hex(uint8_t command){return hex(std::span<const uint8_t>(&command,1));}

struct Snapshot{
    std::vector<std::string> unanswered;
    std::optional<Bytes> read(Speaker& speaker,uint8_t command){
        auto replies=speaker.request(command);
        if(replies.empty()){unanswered.push_back(command_hex(command));return std::nullopt;}
        return replies.front().data;
    }
};
static void raw_field(const char* name,const std::optional<Bytes>& data){
    std::cout<<",\n  "<<quoted(name)<<": "<<(data?quoted(hex(*data)):"null");
}

static VoicePrompts read_prompts(Speaker& speaker){
    auto replies=speaker.request(GetVoicePrompts);
    if(replies.empty())throw std::runtime_error("Voice prompts were not reported.");
    auto prompts=parse_voice_prompts(replies.front().data);
    if(!prompts)throw std::runtime_error("Unexpected voice prompt reply: "+hex(replies.front().data));
    return *prompts;
}
static void write_prompts(Speaker& speaker,const VoicePrompts& prompts){
    auto payload=voice_prompts_payload(prompts);
    if(speaker.request(SetVoicePrompts,payload).empty())throw std::runtime_error("Voice prompt write was not acknowledged.");
    if(read_prompts(speaker)!=prompts)throw std::runtime_error("Voice prompt readback did not match the write.");
}
// Flips the TWS pairing beep (the least audible one), confirms it, then restores the original set.
static void verify_voice_prompts(Speaker& speaker){
    auto original=read_prompts(speaker);
    auto alternate=original;alternate[4]=!alternate[4];
    try{write_prompts(speaker,alternate);}
    catch(...){try{write_prompts(speaker,original);}catch(...){std::cerr<<"Original voice prompts could not be confirmed. Check them in the Tribit app.\n";}throw;}
    try{write_prompts(speaker,original);}
    catch(...){std::cerr<<"Voice prompt test changed TWS Pairing Beep but restoration was not confirmed. Check the Tribit app.\n";throw;}
}

static void snapshot(Speaker& speaker,const PairedSpeaker& device,bool verified_prompts,std::optional<uint8_t> applied,std::chrono::seconds listen){
    Snapshot s;
    auto info=s.read(speaker,GetDeviceInfo);
    auto battery=s.read(speaker,GetBattery);
    auto eq_raw=s.read(speaker,GetEq);
    auto prompts_raw=s.read(speaker,GetVoicePrompts);
    auto countdown=s.read(speaker,GetCountdown);
    auto status09=s.read(speaker,GetStatus09);
    auto auto_shutdown=s.read(speaker,GetAutoShutdown);
    auto buttons=speaker.request(GetEqButtons,{},3);
    if(buttons.empty())s.unanswered.push_back(command_hex(GetEqButtons));
    if(listen.count()>0){
        std::cerr<<"Listening for "<<listen.count()<<" s; press buttons on the speaker now.\n";
        auto deadline=Speaker::Clock::now()+listen;
        while(auto frame=speaker.receive(deadline))speaker.unsolicited.push_back(std::move(*frame));
    }

    std::cout<<"{\n  \"result\": \"connected\",\n  \"device\": "<<quoted(narrow(device.name))
             <<",\n  \"address\": "<<quoted(format_address(device.address));
    std::cout<<",\n  \"battery_percent\": ";
    if(battery&&battery->size()==1)std::cout<<int((*battery)[0]);else std::cout<<"null";
    std::cout<<",\n  \"firmware\": ";
    if(info&&info->size()>=6)std::cout<<quoted(std::to_string((*info)[3])+"."+std::to_string((*info)[4])+"."+std::to_string((*info)[5]));
    else std::cout<<"null";
    raw_field("device_info_raw",info);
    std::cout<<",\n  \"eq\": ";
    if(auto eq=eq_raw?parse_eq(*eq_raw):std::nullopt){
        std::cout<<"{\"preset\": "<<int(eq->preset)<<", \"name\": "<<quoted(preset_name(eq->preset))<<", \"bands_db\": ";
        // Built-in presets report zeroed bands; only the custom curve carries values.
        if(eq->preset==Custom){
            std::cout<<"[";
            for(size_t i=0;i<eq->bands.size();++i)std::cout<<(i?",":"")<<band_db(eq->bands[i]);
            std::cout<<"]";
        }else std::cout<<"null";
        std::cout<<"}";
    }else std::cout<<"null";
    raw_field("eq_raw",eq_raw);
    std::cout<<",\n  \"voice_prompts\": ";
    if(auto prompts=prompts_raw?parse_voice_prompts(*prompts_raw):std::nullopt){
        std::cout<<"{";
        for(size_t i=0;i<prompts->size();++i)std::cout<<(i?", ":"")<<quoted(kVoicePromptNames[i])<<": "<<((*prompts)[i]?"true":"false");
        std::cout<<"}";
    }else std::cout<<"null";
    std::cout<<",\n  \"eq_button\": [";
    for(size_t i=0;i<buttons.size();++i){
        std::cout<<(i?", ":"");
        if(auto slot=parse_button_slot(buttons[i].data))
            std::cout<<"{\"light\": "<<quoted(kLightNames[slot->light])<<", \"preset\": "<<int(slot->preset)<<", \"name\": "<<quoted(preset_name(slot->preset))<<"}";
        else std::cout<<"{\"raw\": "<<quoted(hex(buttons[i].data))<<"}";
    }
    std::cout<<"]";
    raw_field("countdown_raw",countdown);
    raw_field("status_09_raw",status09);
    raw_field("auto_shutdown_raw",auto_shutdown);
    std::cout<<",\n  \"unsolicited\": [";
    for(size_t i=0;i<speaker.unsolicited.size();++i){
        auto& frame=speaker.unsolicited[i];
        std::cout<<(i?", ":"")<<"{\"command\": "<<quoted(command_hex(frame.command))<<", \"data\": "<<quoted(hex(frame.data))<<"}";
    }
    std::cout<<"],\n  \"unanswered\": [";
    for(size_t i=0;i<s.unanswered.size();++i)std::cout<<(i?", ":"")<<quoted(s.unanswered[i]);
    std::cout<<"],\n  \"voice_prompts_change_verified\": "<<(verified_prompts?"true":"false")
             <<",\n  \"preset_applied\": ";
    if(applied)std::cout<<int(*applied);else std::cout<<"null";
    std::cout<<",\n  \"packets_sent\": "<<speaker.tx<<",\n  \"packets_received\": "<<speaker.rx<<"\n}\n";
}

int main(int argc,char** argv){
    std::optional<BTH_ADDR> address;
    std::optional<uint8_t> preset;
    bool verify_prompts=false,trace=false;
    std::chrono::seconds listen{0};
    for(int i=1;i<argc;++i){
        std::string arg=argv[i];
        auto value=[&]()->std::string{if(i+1>=argc)throw std::invalid_argument(arg+" needs a value.");return argv[++i];};
        try{
            if(arg=="--address"){address=parse_address(value());if(!address)throw std::invalid_argument("Use an address like 12:34:56:78:9A:BC.");}
            else if(arg=="--trace")trace=true;
            else if(arg=="--verify-voice-prompts")verify_prompts=true;
            else if(arg=="--listen"){int s=std::stoi(value());if(s<1||s>120)throw std::invalid_argument("--listen takes 1 to 120 seconds.");listen=std::chrono::seconds(s);}
            else if(arg=="--preset"){int id=std::stoi(value());if(id<0||id>5)throw std::invalid_argument("--preset takes 0 to 5.");preset=static_cast<uint8_t>(id);}
            else if(arg=="--help"){
                std::cout<<"tribit-probe [--address AA:BB:CC:DD:EE:FF] [--trace] [--listen SECONDS] [--verify-voice-prompts] [--preset 0-5]\n"
                           "Default: read-only RFCOMM probe of a paired XSound Plus 2; never scans, pairs or retries.\n"
                           "--trace prints every frame sent and received to stderr.\n"
                           "--listen waits for frames the speaker sends on its own (press its buttons meanwhile).\n"
                           "--verify-voice-prompts flips TWS Pairing Beep once, confirms it, then restores the original setting.\n"
                           "--preset applies an EQ preset and is not restored afterwards: 0 XBass Off, 1 XBass,\n"
                           "  2 Audiobook, 3 MaxSound traditional, 4 Rock, 5 Jazz.\n";
                return 0;
            }else{std::cerr<<"Unknown argument. Use --help.\n";return 2;}
        }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 2;}
    }
    try{
        auto device=find_speaker(address);
        Speaker speaker;
        if(trace)speaker.trace=[](const char* direction,std::span<const uint8_t> bytes){std::cerr<<direction<<" "<<hex(bytes)<<"\n";};
        speaker.connect(device.address);
        if(verify_prompts)verify_voice_prompts(speaker);
        if(preset){
            Eq eq{*preset};
            if(speaker.request(SetEq,eq_payload(eq)).empty())throw std::runtime_error("Preset change was not acknowledged.");
            std::this_thread::sleep_for(300ms);
        }
        snapshot(speaker,device,verify_prompts,preset,listen);
        return 0;
    }catch(const std::exception& error){
        std::cout<<"{\"result\":\"failed\",\"error\":"<<quoted(error.what())<<"}\n";
        return 1;
    }
}
