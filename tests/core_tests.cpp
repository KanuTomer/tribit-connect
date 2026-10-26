#include "widget_card.hpp"
#include "widget_artwork.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <iostream>

using winrt::Windows::Data::Json::JsonObject;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}

static Snapshot connected(){
    Snapshot s;s.ready=s.fresh=true;s.status="Connected";s.firmware="0.3.1";s.address="00:11:22:33:44:55";
    s.battery=64;s.preset=tribit::XBass;s.prompts={1,1,1,1,0};s.auto_shutdown=1;s.buttons={0,1,3};s.checked=1;
    return s;
}

int main(){try{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    Snapshot offline;offline.error="The speaker did not accept the control connection. Close the Tribit app on your phone and retry.";
    Snapshot custom=connected();custom.preset=tribit::Custom;
    Snapshot off=connected();off.ready=off.fresh=false;off.status="Not connected";
    Snapshot busy=connected();busy.pending=true;
    Snapshot low=connected();low.battery=12;
    auto view_of=[](WidgetPage page,int details=0){WidgetView view;view.page=page;view.details_page=details;return view;};
    auto parse=[](const std::string& json){return JsonObject::Parse(winrt::to_hstring(json));};
    auto has=[](const std::string& json,const char* text){return json.find(text)!=std::string::npos;};
    auto data=[&](const Snapshot& s,WidgetPage page=WidgetPage::Home,const WidgetView* view=nullptr,const WidgetIcons& icons={}){
        return parse(make_card(s,WidgetSize::Medium,view?*view:view_of(page),icons).data);};
    auto str=[](const JsonObject& o,const wchar_t* key){return winrt::to_string(o.GetNamedString(key));};
    WidgetIcons art;for(int id:kPresetOrder)for(int h:kCurveHeights)art.curves[{id,h}]={"light-"+std::to_string(id),"dark-"+std::to_string(id)+"-"+std::to_string(h)};

    // The template never depends on the speaker's state, so state changes are data-only updates
    // and the host keeps the card and its images instead of rebuilding it (which flashed).
    const WidgetPage pages[]={WidgetPage::Home,WidgetPage::More,WidgetPage::Prompts,WidgetPage::Diagnostics};
    WidgetView chosen;chosen.proposed=Command{Operation::Preset,tribit::Rock};
    for(auto* icons:{&art,static_cast<WidgetIcons*>(nullptr)})
        for(auto size:{WidgetSize::Medium,WidgetSize::Large})
            for(auto page:pages)for(int details=0;details<3;++details){
                auto view=view_of(page,details);WidgetIcons none;const auto& art=icons?*icons:none;
                auto reference=make_card(connected(),size,view,art);
                expect(str(parse(reference.tmpl),L"type")=="AdaptiveCard","Every page and size is a valid card");
                for(auto& state:{offline,custom,off,busy,low,Snapshot{}}){
                    auto card=make_card(state,size,view,art);parse(card.data);
                    expect(card.tmpl==reference.tmpl,"The template is the same in every speaker state");
                }
                auto pending_view=view;pending_view.proposed=chosen.proposed;pending_view.failed_write=true;
                expect(make_card(busy,size,pending_view,art).tmpl==reference.tmpl,"Clicks and failed writes change data only");
            }

    expect(data(offline).GetNamedBoolean(L"showRetry")&&!data(connected()).GetNamedBoolean(L"showRetry"),"Retry only after reconnecting gives up");
    auto large_home=make_card(connected(),WidgetSize::Large).tmpl;
    expect(has(large_home,"eq.slot.5")&&!has(large_home,"media.")&&!has(large_home,"volume."),"Large Home lists presets and has no playback buttons");

    // Speaker switched off: one calm state, no Retry, and the same wording on every page.
    expect(status_label(off)=="Disconnected"&&status_label(offline)=="Controls unavailable"&&status_label(connected())=="Connected","Status wording");
    auto d=data(off);
    expect(str(d,L"status")=="Disconnected"&&str(data(off,WidgetPage::Diagnostics),L"connection")=="Disconnected"&&!d.GetNamedBoolean(L"showRetry"),"Disconnected without Retry");
    expect(str(data(connected()),L"status").empty()&&str(data(connected(),WidgetPage::Diagnostics),L"connection")=="Connected","Healthy Home shows no status");
    // Last-known values are labelled, and speaker settings are not shown as current.
    expect(str(d,L"eqNote")=="Last: XBass"&&str(d.GetNamedObject(L"s1"),L"title")=="XBass, last used"&&data(off,WidgetPage::Home,nullptr,art).GetNamedBoolean(L"hasCurve"),"Last-known preset is labelled");
    expect(str(data(off,WidgetPage::Home,nullptr,art).GetNamedObject(L"curve"),L"dark")=="dark-1-80","Disconnected keeps the last curve");
    auto settings=data(off,WidgetPage::More);
    expect(str(data(off,WidgetPage::Prompts).GetNamedObject(L"q0"),L"value")=="--"&&str(settings.GetNamedObject(L"m1"),L"value")=="--"&&str(settings,L"note")=="Not connected","Speaker settings show -- while disconnected");
    expect(str(settings.GetNamedObject(L"m2"),L"value")=="Off","App preferences keep their values while disconnected");
    expect(!data(custom,WidgetPage::Home,nullptr,art).GetNamedBoolean(L"hasCurve")&&str(data(custom),L"eqNote")=="Custom curve","A custom curve without artwork shows no image");
    expect(str(parse(make_card(connected(),WidgetSize::Large,{},art).data).GetNamedObject(L"curve"),L"dark")=="dark-1-96","Each size gets its own curve height");
    expect(str(data(low),L"batteryColor")=="Attention"&&str(data(low),L"batteryText")=="12%","Low battery is marked");
    // Medium settings and prompts fit on one page; the Bluetooth link moved to Device.
    expect(!has(make_card(connected(),WidgetSize::Medium,view_of(WidgetPage::More)).tmpl,"view.details.next"),"Medium settings fit one page");
    expect(!has(make_card(connected(),WidgetSize::Medium,view_of(WidgetPage::Prompts)).tmpl,"view.details.next"),"Medium prompts fit one page");
    expect(has(make_card(connected(),WidgetSize::Large,view_of(WidgetPage::Diagnostics)).tmpl,"open.bluetooth")&&str(data(connected(),WidgetPage::Diagnostics),L"lastSeen").find("Last")==std::string::npos,"Device links Bluetooth settings without repeated wording");
    expect(data(offline,WidgetPage::Diagnostics).GetNamedBoolean(L"hasError")&&!data(connected(),WidgetPage::Diagnostics).GetNamedBoolean(L"hasError"),"Diagnostic row only with an error");

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
    {   // Sound Lab can put a saved curve on a widget button.
        auto slots=default_slots();slots[2]={tribit::Custom,{3,2,1,0,0,0,1,2,3},"Night Desk"};
        auto live=connected();
        expect(!setting_command(live,L"eq.slot.1",slots)&&!setting_command(live,L"eq.slot.6",slots),"The active button and unknown buttons do nothing");
        auto saved=setting_command(live,L"eq.slot.2",slots);
        expect(saved&&saved->operation==Operation::CustomEq&&saved->bands==slots[2].bands,"A saved-curve button writes its curve");
        live.preset=tribit::Custom;live.bands=slots[2].bands;
        expect(!setting_command(live,L"eq.slot.2",slots),"The playing saved curve does nothing");
        auto d=parse(make_card(live,WidgetSize::Medium,{},art,false,slots).data);
        expect(str(d.GetNamedObject(L"s2"),L"label")=="Night Desk"&&str(d.GetNamedObject(L"s2"),L"color")=="Accent"&&str(d,L"eqNote")=="Night Desk","The playing saved curve is named and highlighted");
        WidgetIcons drawn=art;custom_artwork(drawn,slots[2].bands,false);
        expect(parse(make_card(live,WidgetSize::Medium,{},drawn,false,slots).data).GetNamedBoolean(L"hasCurve"),"Saved curves are drawn");
        live.bands[0]=-8;
        expect(str(parse(make_card(live,WidgetSize::Medium,{},art,false,slots).data).GetNamedObject(L"s2"),L"color")=="Default","Another custom curve highlights nothing");
    }

    WidgetView view;
    expect(navigate_widget(view,L"view.prompts")&&view.page==WidgetPage::Prompts,"Navigate to prompts");
    expect(navigate_widget(view,L"view.details.next")&&view.details_page==1,"Page forward");
    // Medium is an EQ switcher first: all six presets on Home, settings behind one button.
    auto medium=make_card(connected(),WidgetSize::Medium).tmpl;
    expect(has(medium,"eq.slot.0")&&has(medium,"eq.slot.5")&&has(medium,"view.more")&&!has(medium,"view.eq"),"Medium Home lists every preset");
    // Only the selected preset's text takes the accent colour; no images are involved.
    { auto t=data(connected());
      expect(str(t.GetNamedObject(L"s1"),L"color")=="Accent"&&str(t.GetNamedObject(L"s0"),L"color")=="Default","Only the selected preset is highlighted");
      expect(!has(make_card(connected(),WidgetSize::Medium,{},art).tmpl,"backgroundImage"),"Buttons carry no background images"); }
    // A click shows its result before the speaker confirms it.
    { auto t=data(busy,WidgetPage::Home,&chosen);
      expect(str(t.GetNamedObject(L"s3"),L"title")=="Rock, selected"&&str(data(busy,WidgetPage::Home,&chosen,art).GetNamedObject(L"curve"),L"dark")=="dark-4-80","Chosen preset and its curve show at once");
      // The confirmation must not differ, or it becomes a second update (each one rebuilds the card).
      auto confirmed=connected();confirmed.preset=tribit::Rock;
      for(auto page:pages){auto pending_view=view_of(page);pending_view.proposed=chosen.proposed;
          expect(make_card(busy,WidgetSize::Medium,pending_view,art).data==make_card(confirmed,WidgetSize::Medium,view_of(page),art).data,"A confirmed click sends no second update");}
      // A background refresh on a working link must not change the card either.
      auto refreshing=connected();refreshing.pending=true;refreshing.status="Refreshing...";
      for(auto page:pages)expect(make_card(refreshing,WidgetSize::Medium,view_of(page),art).data==make_card(connected(),WidgetSize::Medium,view_of(page),art).data,"A refresh sends no update"); }
    view.proposed=Command{Operation::Preset,tribit::Rock};
    reconcile_widget(view,offline);
    expect(view.failed_write&&!view.proposed,"An unconfirmed write is reported once it resolves");
    reconcile_widget(view,connected());
    expect(!view.failed_write,"A fresh healthy read clears the failure");
    std::cout<<"Passed: state-independent templates, data for every state, command mapping, navigation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<"\n";return 1;}}
