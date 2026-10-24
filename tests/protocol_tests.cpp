#include "protocol.hpp"
#include <iostream>

using namespace tribit;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}

int main(){try{
    // Captured from XSound Plus 2 firmware 0.3.1 on 2026-10-25.
    expect(encode(GetBattery)==Bytes({0x08,0xEE,0x00,0x00,0x00,0x01,0x02,0x0A,0x00,0x03}),"Battery request must match capture");
    const Bytes battery_reply{0x09,0xFF,0x00,0x00,0x01,0x01,0x02,0x0B,0x00,0x64,0x7B};
    expect(encode(GetBattery,Bytes{0x64},true)==battery_reply,"Encoder must reproduce the captured reply");

    Parser parser;
    parser.feed(Bytes{0x55,0x09,0xFF});                         // noise, then a split header
    expect(!parser.next(),"Partial frame must wait");
    parser.feed({battery_reply.data()+2,battery_reply.size()-2});
    auto frame=parser.next();
    expect(frame&&frame->command==GetBattery&&frame->data==Bytes{0x64},"Split frame must reassemble");
    expect(parser.skipped()==1,"Only the noise byte may be skipped");

    auto corrupt=battery_reply;corrupt.back()^=1;
    parser.feed(corrupt);parser.feed(encode(GetCountdown,Bytes{0x00},true));
    frame=parser.next();
    expect(frame&&frame->command==GetCountdown&&parser.rejected()==1,"Bad checksum must be dropped, next frame kept");
    expect(!parser.next(),"Nothing may remain");

    parser.feed(encode(SetEq,{},true));
    frame=parser.next();
    expect(frame&&frame->command==SetEq&&frame->data.empty(),"Set acknowledgement is an empty echo");

    auto eq=parse_eq(Bytes{0xFE,0x0B,8,8,8,8,8,8,8,8});
    expect(eq&&eq->preset==Custom&&band_db(eq->bands[0])==3&&band_db(eq->bands[8])==0,"Custom EQ +3 dB at 80 Hz");
    expect(eq_payload(*eq)==Bytes({0xFE,0x0B,8,8,8,8,8,8,8,8}),"Custom EQ payload round-trips");
    expect(eq_payload({Jazz})==Bytes({5,0,0,0,0,0,0,0,0,0}),"Preset payload is id plus nine zeros");
    expect(!parse_eq(Bytes{0xFE,8}),"Short EQ must be rejected");

    auto prompts=parse_voice_prompts(Bytes{0,0,0,0,1});
    expect(prompts&&!(*prompts)[0]&&(*prompts)[4],"Voice prompts parse");
    expect(voice_prompts_payload(*prompts)==Bytes({0,0,0,0,1}),"Voice prompts round-trip");
    expect(!parse_voice_prompts(Bytes{0,2,0,0,0}),"Non-boolean prompt must be rejected");

    auto write=eq_buttons_write({XBassOff,XBass,MaxSound});
    expect(write.size()==3*(kOverhead+12),"Button write is three frames");
    Parser buttons;buttons.feed(write);  // host header is not a speaker frame
    expect(!buttons.next(),"Host frames must not parse as speaker frames");
    Bytes slot{0x03,2,MaxSound};slot.resize(12,0);
    auto parsed=parse_button_slot(slot);
    expect(parsed&&parsed->light==2&&parsed->preset==MaxSound,"Button slot parses");
    expect(preset_name(2)=="Audiobook"&&preset_name(4)=="Rock","Preset ids confirmed on the speaker");
    expect(hex(Bytes{0x0A,0xFF})=="0A FF","Hex formatting");

    std::cout<<"Passed: captured framing, reassembly, checksum rejection, EQ, voice prompts, EQ button.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
