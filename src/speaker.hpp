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

struct PairedSpeaker{std::wstring name;BTH_ADDR address{};bool connected{};};

// Looks only at devices Windows already knows; never scans or pairs.
inline PairedSpeaker find_speaker(std::optional<BTH_ADDR> wanted){
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof search};
    search.fReturnAuthenticated=TRUE;search.fReturnRemembered=TRUE;search.fReturnConnected=TRUE;search.fIssueInquiry=FALSE;
    BLUETOOTH_DEVICE_INFO info{sizeof info};
    HBLUETOOTH_DEVICE_FIND find=BluetoothFindFirstDevice(&search,&info);
    if(!find)throw std::runtime_error("No paired Bluetooth devices were found. Pair the speaker in Windows Bluetooth settings.");
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

class Speaker{
public:
    using Clock=std::chrono::steady_clock;
    std::function<void(const char*,std::span<const uint8_t>)> trace;
    size_t tx{},rx{};

    Speaker(){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock could not start.");}
    ~Speaker(){if(socket_!=INVALID_SOCKET)closesocket(socket_);WSACleanup();}
    Speaker(const Speaker&)=delete;Speaker& operator=(const Speaker&)=delete;

    void connect(BTH_ADDR address){
        socket_=::socket(AF_BTH,SOCK_STREAM,BTHPROTO_RFCOMM);
        if(socket_==INVALID_SOCKET)throw std::runtime_error("Could not create a Bluetooth socket.");
        SOCKADDR_BTH target{};target.addressFamily=AF_BTH;target.btAddr=address;target.port=kRfcommChannel;
        if(::connect(socket_,reinterpret_cast<sockaddr*>(&target),sizeof target)==SOCKET_ERROR)
            throw std::runtime_error("RFCOMM connection failed (error "+std::to_string(WSAGetLastError())+
                "). Turn the speaker on and close the Tribit app on your phone.");
        // The speaker drops a frame sent straight after the channel opens.
        Sleep(500);
    }
    void send(std::span<const uint8_t> bytes){
        if(trace)trace("tx",bytes);
        size_t sent=0;
        while(sent<bytes.size()){
            int n=::send(socket_,reinterpret_cast<const char*>(bytes.data()+sent),static_cast<int>(bytes.size()-sent),0);
            if(n==SOCKET_ERROR)throw std::runtime_error("Bluetooth send failed (error "+std::to_string(WSAGetLastError())+").");
            sent+=n;
        }
        ++tx;
    }
    // Next speaker frame, or nullopt once the deadline passes.
    std::optional<Frame> receive(Clock::time_point deadline){
        for(;;){
            if(auto frame=parser_.next()){++rx;if(trace)trace("rx",encode(frame->command,frame->data,true));return frame;}
            auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
            if(left<=0)return std::nullopt;
            fd_set read;FD_ZERO(&read);FD_SET(socket_,&read);
            timeval wait{static_cast<long>(left/1000),static_cast<long>(left%1000*1000)};
            int ready=::select(0,&read,nullptr,nullptr,&wait);
            if(ready==SOCKET_ERROR)throw std::runtime_error("Bluetooth wait failed.");
            if(!ready)return std::nullopt;
            uint8_t chunk[512];
            int n=::recv(socket_,reinterpret_cast<char*>(chunk),sizeof chunk,0);
            if(n==0)throw std::runtime_error("The speaker closed the connection.");
            if(n==SOCKET_ERROR)throw std::runtime_error("Bluetooth receive failed (error "+std::to_string(WSAGetLastError())+").");
            parser_.feed({chunk,static_cast<size_t>(n)});
        }
    }
    // Sends one frame and collects up to `count` replies for that command; anything else is kept as unsolicited.
    std::vector<Frame> request(uint8_t command,std::span<const uint8_t> data={},size_t count=1,std::chrono::milliseconds timeout=std::chrono::milliseconds(1500)){
        send(encode(command,data));
        return collect(command,count,timeout);
    }
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
    std::deque<Frame> unsolicited;
private:
    SOCKET socket_{INVALID_SOCKET};
    Parser parser_;
};
}
