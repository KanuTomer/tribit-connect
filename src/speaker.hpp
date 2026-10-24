#pragma once
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include "protocol.hpp"
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>

using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;

static std::string quoted(const std::string& text){
    std::string out="\"";
    for(char c:text){
        if(c=='"'||c=='\\'){out+='\\';out+=c;}
        else if(static_cast<unsigned char>(c)<0x20){char esc[8];std::snprintf(esc,sizeof esc,"\\u%04x",static_cast<unsigned>(static_cast<unsigned char>(c)));out+=esc;}
        else out+=c;
    }
    return out+"\"";
}

namespace tribit {
inline std::string narrow(const std::wstring& text){
    if(text.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
inline std::string format_address(BTH_ADDR address){
    char text[18];
    std::snprintf(text,sizeof text,"%02X:%02X:%02X:%02X:%02X:%02X",
        static_cast<unsigned>((address>>40)&0xFF),static_cast<unsigned>((address>>32)&0xFF),static_cast<unsigned>((address>>24)&0xFF),
        static_cast<unsigned>((address>>16)&0xFF),static_cast<unsigned>((address>>8)&0xFF),static_cast<unsigned>(address&0xFF));
    return text;
}
inline std::optional<BTH_ADDR> parse_address(const std::string& text){
    unsigned b[6];char tail;
    if(sscanf_s(text.c_str(),"%2x:%2x:%2x:%2x:%2x:%2x%c",&b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&tail,1u)!=6)return std::nullopt;
    BTH_ADDR address=0;for(unsigned byte:b)address=(address<<8)|byte;return address;
}

// A paired speaker that Windows has not connected: wait for the link instead of retrying.
struct NotConnected:std::runtime_error{using std::runtime_error::runtime_error;};
struct PairedSpeaker{std::wstring name;BTH_ADDR address{};bool connected{};};

// Looks only at devices Windows already knows; never scans or pairs.
inline PairedSpeaker find_speaker(std::optional<BTH_ADDR> wanted){
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof search};
    search.fReturnAuthenticated=TRUE;search.fReturnRemembered=TRUE;search.fReturnConnected=TRUE;search.fIssueInquiry=FALSE;
    BLUETOOTH_DEVICE_INFO info{sizeof info};
    HBLUETOOTH_DEVICE_FIND find=BluetoothFindFirstDevice(&search,&info);
    // Windows sometimes returns an empty search while a speaker powers down; for the
    // remembered speaker that means "not connected", not "never paired".
    if(!find){if(wanted)throw NotConnected("Speaker not connected.");throw std::runtime_error("No paired Bluetooth devices were found. Pair the speaker in Windows Bluetooth settings.");}
    std::optional<PairedSpeaker> match;
    do{
        std::wstring name=info.szName;
        bool hit=wanted?info.Address.ullLong==*wanted:name.find(L"XSound Plus 2")!=std::wstring::npos;
        if(hit&&(!match||(!match->connected&&info.fConnected)))match=PairedSpeaker{name,info.Address.ullLong,info.fConnected!=FALSE};
        info.dwSize=sizeof info;
    }while(BluetoothFindNextDevice(find,&info));
    BluetoothFindDeviceClose(find);
    if(!match)throw std::runtime_error(wanted?"That address is not a paired device.":"No paired Tribit XSound Plus 2 was found. Pair it in Windows Bluetooth settings, or pass --address.");
    return *match;
}

// Event-driven RFCOMM link: an idle connection sleeps on wait_handle() until the speaker sends data.
class Speaker{
public:
    std::function<void(const char*,std::span<const uint8_t>)> trace;
    std::deque<Frame> unsolicited;
    size_t tx{},rx{};

    Speaker(){
        WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock could not start.");
        event_=WSACreateEvent();if(event_==WSA_INVALID_EVENT){WSACleanup();throw std::runtime_error("Could not create a socket event.");}
    }
    ~Speaker(){if(socket_!=INVALID_SOCKET){shutdown(socket_,SD_BOTH);closesocket(socket_);}WSACloseEvent(event_);WSACleanup();}
    Speaker(const Speaker&)=delete;Speaker& operator=(const Speaker&)=delete;
    HANDLE wait_handle()const{return event_;}

    void connect(BTH_ADDR address,std::chrono::milliseconds timeout=10s){
        socket_=::socket(AF_BTH,SOCK_STREAM,BTHPROTO_RFCOMM);
        if(socket_==INVALID_SOCKET)throw std::runtime_error("Could not create a Bluetooth socket.");
        if(WSAEventSelect(socket_,event_,FD_CONNECT|FD_READ|FD_CLOSE)==SOCKET_ERROR)fail("Bluetooth socket setup failed");
        SOCKADDR_BTH target{};target.addressFamily=AF_BTH;target.btAddr=address;target.port=kRfcommChannel;
        if(::connect(socket_,reinterpret_cast<sockaddr*>(&target),sizeof target)==SOCKET_ERROR){
            if(WSAGetLastError()!=WSAEWOULDBLOCK)fail("RFCOMM connection failed");
            // A phone holding the control channel makes Windows wait about 20 s; give up sooner.
            if(WaitForSingleObject(event_,static_cast<DWORD>(timeout.count()))!=WAIT_OBJECT_0)
                throw std::runtime_error("The speaker did not accept the control connection. Close the Tribit app on your phone and retry.");
            WSANETWORKEVENTS events{};
            if(WSAEnumNetworkEvents(socket_,event_,&events)==SOCKET_ERROR)fail("RFCOMM connection failed");
            if(!(events.lNetworkEvents&FD_CONNECT)||events.iErrorCode[FD_CONNECT_BIT])
                throw std::runtime_error("RFCOMM connection failed (error "+std::to_string(events.iErrorCode[FD_CONNECT_BIT])+
                    "). Turn the speaker on and close the Tribit app on your phone.");
        }
        // The speaker drops a frame sent straight after the channel opens.
        Sleep(500);
    }
    void send(std::span<const uint8_t> bytes){
        if(trace)trace("tx",bytes);
        size_t sent=0;int waits=0;
        while(sent<bytes.size()){
            int n=::send(socket_,reinterpret_cast<const char*>(bytes.data()+sent),static_cast<int>(bytes.size()-sent),0);
            if(n!=SOCKET_ERROR){sent+=n;continue;}
            if(WSAGetLastError()!=WSAEWOULDBLOCK||++waits>100)fail("Bluetooth send failed");
            Sleep(10);
        }
        ++tx;
    }
    // Next speaker frame, or nullopt once the deadline passes. A past deadline only drains buffered data.
    std::optional<Frame> receive(Clock::time_point deadline){
        bool reset=false;
        for(;;){
            if(auto frame=parser_.next()){++rx;if(trace)trace("rx",encode(frame->command,frame->data,true));return frame;}
            uint8_t chunk[512];
            int n=::recv(socket_,reinterpret_cast<char*>(chunk),sizeof chunk,0);
            if(n>0){parser_.feed({chunk,static_cast<size_t>(n)});reset=false;continue;}
            if(n==0)throw NotConnected("The speaker closed the control connection.");
            if(WSAGetLastError()!=WSAEWOULDBLOCK)fail("Bluetooth receive failed");
            // Clear the event before sleeping on it, then read once more so data that
            // arrived in between is not missed and an idle socket never spins.
            if(!reset){
                WSANETWORKEVENTS events{};
                if(WSAEnumNetworkEvents(socket_,event_,&events)==SOCKET_ERROR)fail("Bluetooth wait failed");
                if(events.lNetworkEvents&FD_CLOSE)throw NotConnected("The speaker closed the control connection.");
                reset=true;continue;
            }
            auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
            if(left<=0)return std::nullopt;
            if(WaitForSingleObject(event_,static_cast<DWORD>(left))!=WAIT_OBJECT_0)return std::nullopt;
            reset=false;
        }
    }
    // Collects up to `count` replies for `command`; other frames are kept as unsolicited.
    std::vector<Frame> collect(uint8_t command,size_t count,std::chrono::milliseconds timeout){
        std::vector<Frame> replies;
        auto deadline=Clock::now()+timeout;
        while(replies.size()<count){
            auto frame=receive(deadline);
            if(!frame)break;
            if(frame->command==command)replies.push_back(std::move(*frame));else unsolicited.push_back(std::move(*frame));
        }
        return replies;
    }
    std::vector<Frame> request(uint8_t command,std::span<const uint8_t> data={},size_t count=1,std::chrono::milliseconds timeout=1500ms){
        send(encode(command,data));
        return collect(command,count,timeout);
    }
private:
    SOCKET socket_{INVALID_SOCKET};
    WSAEVENT event_{WSA_INVALID_EVENT};
    Parser parser_;
    [[noreturn]] static void fail(const char* what){throw std::runtime_error(std::string(what)+" (error "+std::to_string(WSAGetLastError())+").");}
};
}
