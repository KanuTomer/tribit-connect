// Exports every widget card the provider can send, for the browser mock in tools/widget-mock.html.
// Cards come from the same make_card and artwork code as the Widgets provider.
#include <winsock2.h>
#include <windows.h>
#include <unknwn.h>
#include "widget_card.hpp"
#include "widget_artwork.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace fs=std::filesystem;
static std::vector<uint8_t> decode_png(const std::string& uri){
    static const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;uint32_t buffer{};int bits{};
    for(char c:uri.substr(uri.find(',')+1)){if(c=='=')break;buffer=(buffer<<6)|static_cast<uint32_t>(alphabet.find(c));bits+=6;if(bits>=8){bits-=8;out.push_back(static_cast<uint8_t>(buffer>>bits));}}
    return out;
}
// Generated images become files, so cards stay small and the page loads quickly.
struct ArtFiles{
    fs::path folder;std::map<std::string,std::string> names;
    std::string operator()(const std::string& uri){
        if(!uri.starts_with("data:"))return uri;
        auto found=names.find(uri);if(found!=names.end())return found->second;
        auto name="art/"+std::to_string(names.size())+".png";auto bytes=decode_png(uri);
        std::ofstream(folder/name,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        return names[uri]=name;
    }
    void apply(WidgetIcons& icons){
        for(auto& [key,urls]:icons.symbols){urls.first=(*this)(urls.first);urls.second=(*this)(urls.second);}
        for(auto& [key,urls]:icons.curves){urls.first=(*this)(urls.first);urls.second=(*this)(urls.second);}    }
};

int main(int argc,char** argv){try{
    if(argc<2){std::cerr<<"widget-mock <output folder>\n";return 2;}
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    fs::path out=argv[1];fs::create_directories(out/"art");
    ArtFiles art{out};
    WidgetIcons base;load_artwork(base,[](const std::string& file){return "icons/"+file;},false,true);art.apply(base);

    Snapshot connected;connected.ready=connected.fresh=true;connected.status="Connected";connected.firmware="0.3.1";connected.address="00:11:22:33:44:55";
    connected.battery=90;connected.preset=tribit::XBass;connected.prompts={1,1,1,1,0};connected.auto_shutdown=1;connected.buttons={0,1,3};connected.checked=1761341700;
    std::vector<std::pair<std::string,Snapshot>> states;
    for(auto id:kPresetOrder){auto s=connected;s.preset=id;states.emplace_back("connected-"+std::to_string(id),s);}
    auto with=[&](auto change){auto s=connected;change(s);return s;};
    states.emplace_back("low-battery",with([](Snapshot& s){s.battery=12;}));
    states.emplace_back("custom-eq",with([](Snapshot& s){s.preset=tribit::Custom;}));
    states.emplace_back("updating",with([](Snapshot& s){s.pending=true;s.status="Refreshing...";}));
    states.emplace_back("connecting",with([](Snapshot& s){s.ready=s.fresh=false;s.pending=true;s.status="Connecting...";}));
    states.emplace_back("disconnected",with([](Snapshot& s){s.ready=s.fresh=false;s.status="Not connected";}));
    states.emplace_back("unavailable",with([](Snapshot& s){s.ready=s.fresh=false;s.status="Controls unavailable";s.error="The speaker did not accept the control connection. Close the Tribit app on your phone and retry.";}));
    states.emplace_back("never-connected",Snapshot{});

    const std::pair<const char*,WidgetPage> pages[]={{"home",WidgetPage::Home},{"more",WidgetPage::More},{"prompts",WidgetPage::Prompts},{"device",WidgetPage::Diagnostics}};
    const std::pair<const char*,WidgetSize> sizes[]={{"medium",WidgetSize::Medium},{"large",WidgetSize::Large}};
    std::ofstream js(out/"cards.js",std::ios::binary);
    js<<"window.MOCK_CARDS={";
    size_t count=0;
    for(auto& [state_name,state]:states){
        auto icons=base;
        for(auto& [size_name,size]:sizes)for(auto& [page_name,page]:pages){
            std::string previous;
            for(int detail=0;detail<6;++detail){
                WidgetView view;view.page=page;view.details_page=detail;
                // Template and data are kept apart, as the provider sends them.
                auto card=make_card(state,size,view,icons,false);
                if(card.tmpl==previous)break; // past the last page
                js<<(count++?",\n":"\n")<<::quoted(state_name+"|"+size_name+"|"+page_name+"|"+std::to_string(detail))<<":{\"t\":"<<card.tmpl<<",\"d\":"<<card.data<<"}";
                previous=std::move(card.tmpl);
            }
        }
    }
    js<<"\n};\n";
    std::cout<<count<<" cards, "<<art.names.size()<<" images -> "<<out.string()<<"\n";
    return 0;
}catch(const winrt::hresult_error& e){std::cerr<<winrt::to_string(e.message())<<"\n";return 1;}
catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
