#include "widget_card.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <iostream>

using winrt::Windows::Data::Json::JsonObject;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}

static Snapshot connected(){
    Snapshot s;s.ready=s.fresh=true;s.status="Connected";s.firmware="0.3.1";s.address="F4:4E:FC:C3:DD:50";
    s.battery=64;s.preset=tribit::XBass;s.prompts={1,1,1,1,0};s.auto_shutdown=1;s.buttons={0,1,3};s.checked=1;
    return s;
}

int main(){try{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    Snapshot offline;offline.error="The speaker did not accept the control connection. Close the Tribit app on your phone and retry.";
    Snapshot custom=connected();custom.preset=tribit::Custom;
    const WidgetPage pages[]={WidgetPage::Home,WidgetPage::More,WidgetPage::Prompts,WidgetPage::Diagnostics};
    for(auto& state:{connected(),offline,custom,Snapshot{}})
        for(auto size:{WidgetSize::Medium,WidgetSize::Large})
            for(auto page:pages)for(int details=0;details<3;++details){
                WidgetView view;view.page=page;view.details_page=details;
                auto card=make_card(state,size,view);
                expect(JsonObject::Parse(winrt::to_hstring(card)).GetNamedString(L"type")==L"AdaptiveCard","Every page and size must be a valid card");
            }
    auto has=[](const std::string& card,const char* text){return card.find(text)!=std::string::npos;};
    auto view_of=[](WidgetPage page){WidgetView view;view.page=page;return view;};
    expect(has(make_card(offline,WidgetSize::Medium),"\"retry\""),"A refused control channel offers Retry");
    expect(!has(make_card(connected(),WidgetSize::Medium),"\"retry\""),"A healthy connection hides Retry");
    expect(has(make_card(connected(),WidgetSize::Large),"eq.preset.4"),"Large Home lists presets");
    for(auto size:{WidgetSize::Medium,WidgetSize::Large})
        expect(!has(make_card(connected(),size),"media.")&&!has(make_card(connected(),size),"volume."),"Playback and volume buttons are gone");

    // Speaker switched off: one calm state, no Retry, and the same wording on every page.
    Snapshot off=connected();off.ready=off.fresh=false;off.status="Not connected";
    expect(status_label(off)=="Disconnected"&&status_label(offline)=="Controls unavailable"&&status_label(connected())=="Connected","Status wording");
    for(auto page:{WidgetPage::Home,WidgetPage::Diagnostics}){
        auto card=make_card(off,WidgetSize::Medium,view_of(page));
        expect(has(card,"Disconnected")&&!has(card,"Controls unavailable")&&!has(card,"\"retry\""),"Every page says Disconnected without Retry");
    }
    // Last-known values are labelled, and speaker settings are not shown as current.
    expect(has(make_card(off,WidgetSize::Large),"Last: XBass")&&has(make_card(off,WidgetSize::Medium),"XBass, last used")&&!has(make_card(off,WidgetSize::Medium),"Disconnected · "),"Home labels the last-known preset; status has no time");
    auto prompts_off=make_card(off,WidgetSize::Medium,view_of(WidgetPage::Prompts));
    expect(has(prompts_off,"\"--\"")&&!has(prompts_off,"\"On\""),"Voice prompts show -- while disconnected");
    // The status sits in the Battery row, so Home has the same number of rows in every state.
    auto rows=[](const std::string& card){return JsonObject::Parse(winrt::to_hstring(card)).GetNamedArray(L"body").Size();};
    for(auto size:{WidgetSize::Medium,WidgetSize::Large})
        expect(rows(make_card(connected(),size))==rows(make_card(off,size))&&rows(make_card(connected(),size))==rows(make_card(offline,size)),"Home layout does not change with connection state");
    // Medium settings and prompts fit on one page; the Bluetooth link moved to Device.
    expect(!has(make_card(connected(),WidgetSize::Medium,view_of(WidgetPage::More)),"view.details.next"),"Medium settings fit one page");
    expect(!has(make_card(connected(),WidgetSize::Medium,view_of(WidgetPage::Prompts)),"view.details.next"),"Medium prompts fit one page");
    auto device=make_card(connected(),WidgetSize::Large,view_of(WidgetPage::Diagnostics));
    expect(has(device,"open.bluetooth")&&!has(device,"Last seen Last"),"Device links Bluetooth settings without repeated wording");

    auto s=connected();
    expect(!setting_command(s,L"eq.preset.1"),"Selecting the active preset does nothing");
    auto preset=setting_command(s,L"eq.preset.4");
    expect(preset&&preset->operation==Operation::Preset&&preset->value==tribit::Rock,"Preset verb maps to the preset id");
    expect(!setting_command(s,L"eq.preset.6")&&!setting_command(s,L"eq.preset.254"),"Unknown and custom ids are rejected");
    auto prompt=setting_command(s,L"prompt.4");
    expect(prompt&&prompt->operation==Operation::Prompt&&prompt->index==4&&prompt->value==1,"Prompt verb toggles that beep");
    auto shutdown=setting_command(s,L"pref.autoshutdown");
    expect(shutdown&&shutdown->value==0,"Auto shutdown toggles off");
    s.pending=true;
    expect(!setting_command(s,L"eq.preset.4"),"No command while another is pending");
    expect(!setting_command(offline,L"eq.preset.4"),"No command while disconnected");

    WidgetView view;
    expect(navigate_widget(view,L"view.prompts")&&view.page==WidgetPage::Prompts,"Navigate to prompts");
    expect(navigate_widget(view,L"view.details.next")&&view.details_page==1,"Page forward");
    // Medium is an EQ switcher first: all six presets on Home, settings behind one button.
    auto medium=make_card(connected(),WidgetSize::Medium);
    expect(has(medium,"eq.preset.0")&&has(medium,"eq.preset.5")&&has(medium,"view.more")&&!has(medium,"view.eq"),"Medium Home lists every preset");
    // Only the selected preset is tinted; the rest keep the host's neutral surface.
    { WidgetIcons tinted;for(int id:kPresetOrder)tinted.fills[id]={"light-"+std::to_string(id),"dark-"+std::to_string(id)};
      auto card=make_card(connected(),WidgetSize::Medium,{},tinted);
      expect(has(card,"dark-1")&&!has(card,"dark-0")&&!has(card,"dark-5"),"Only the selected preset takes its colour"); }
    // A click shows its result before the speaker confirms it.
    { auto busy=connected();busy.pending=true;WidgetView chosen;chosen.proposed=Command{Operation::Preset,tribit::Rock};
      expect(has(make_card(busy,WidgetSize::Medium,chosen),"Rock, selected"),"Chosen preset is highlighted at once"); }
    view.proposed=Command{Operation::Preset,tribit::Rock};
    reconcile_widget(view,offline);
    expect(view.failed_write&&!view.proposed,"An unconfirmed write is reported once it resolves");
    reconcile_widget(view,connected());
    expect(!view.failed_write,"A fresh healthy read clears the failure");
    std::cout<<"Passed: cards for every page and size, retry visibility, command mapping, navigation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<"\n";return 1;}}
