#pragma once
#include "controller.hpp"
#include <winrt/Microsoft.Windows.Widgets.h>
using winrt::Microsoft::Windows::Widgets::WidgetSize;
#include <map>
#include <optional>

enum class WidgetPage { Home, More, Prompts, Diagnostics };
// Curve heights on the Medium and Large Home pages; images are drawn at these exact sizes.
inline constexpr std::array<int,2> kCurveHeights{80,96};
struct WidgetView {
    WidgetPage page=WidgetPage::Home;
    int details_page{};
    std::optional<Command> proposed;
    bool failed_write{};
};
struct WidgetIcons {
    std::map<std::string,std::pair<std::string,std::string>> symbols;
    std::map<std::pair<int,int>,std::pair<std::string,std::string>> curves; // (preset id, height) -> light/dark EQ curve
    std::map<std::pair<Bands,int>,std::pair<std::string,std::string>> custom_curves; // (bands, height) -> saved curves
};
// The six program buttons on the Home page. Each holds a factory preset or a curve saved in
// Sound Lab (preset Custom with its bands); Sound Lab chooses them, the presets are the default.
struct WidgetSlot { int preset=-1; Bands bands{}; std::string name; };
using WidgetSlots=std::array<WidgetSlot,6>;
inline WidgetSlots default_slots(){WidgetSlots slots;for(size_t i=0;i<slots.size();++i)slots[i]={kPresetOrder[i],{},preset_label(kPresetOrder[i])};return slots;}
inline bool same_eq(int preset,const Bands& bands,const WidgetSlot& slot){return preset==slot.preset&&(preset!=tribit::Custom||bands==slot.bands);}
static bool battery_low(int value){return value>=0&&value<=20;}
static bool navigate_widget(WidgetView& view,std::wstring_view verb) {
    if(verb==L"view.home")view.page=WidgetPage::Home;
    else if(verb==L"view.more"){view.page=WidgetPage::More;view.details_page=0;}
    else if(verb==L"view.prompts"){view.page=WidgetPage::Prompts;view.details_page=0;}
    else if(verb==L"view.diagnostics"){view.page=WidgetPage::Diagnostics;view.details_page=0;}
    else if(verb==L"view.details.next")++view.details_page;
    else if(verb==L"view.details.previous")view.details_page=std::max(0,view.details_page-1);
    else return false;
    return true;
}
static void reconcile_widget(WidgetView& view,const Snapshot& state){
    if(view.proposed&&!state.pending){view.failed_write=!state.error.empty();view.proposed.reset();}
    if(!state.pending&&state.ready&&state.fresh&&state.error.empty())view.failed_write=false;
}
static int suffix_number(std::wstring_view verb,std::wstring_view prefix){
    if(!verb.starts_with(prefix)||verb.size()==prefix.size())return -1;
    int value{};for(auto ch:verb.substr(prefix.size())){if(ch<L'0'||ch>L'9'||value>100)return -1;value=value*10+(ch-L'0');}return value;
}
// Maps a widget verb to a speaker command, or nothing when it would not change anything.
static std::optional<Command> setting_command(const Snapshot& state,std::wstring_view verb,const WidgetSlots& slots=default_slots()){
    if(!state.ready||!state.fresh||state.pending)return {};
    if(auto index=suffix_number(verb,L"eq.slot.");index>=0){
        if(index>=static_cast<int>(slots.size()))return {};
        auto& slot=slots[index];
        if(same_eq(state.preset,state.bands,slot))return {};
        if(slot.preset==tribit::Custom){Command command{Operation::CustomEq};command.bands=slot.bands;return command;}
        if(!valid_preset(slot.preset))return {};
        return Command{Operation::Preset,slot.preset};
    }
    if(auto preset=suffix_number(verb,L"eq.preset.");preset>=0){
        if(!valid_preset(preset)||state.preset==preset)return {};
        return Command{Operation::Preset,preset};
    }
    if(auto prompt=suffix_number(verb,L"prompt.");prompt>=0){
        if(prompt>4||state.prompts[prompt]<0)return {};
        return Command{Operation::Prompt,state.prompts[prompt]?0:1,prompt};
    }
    if(verb==L"pref.autoshutdown"&&state.auto_shutdown>=0)return Command{Operation::AutoShutdown,state.auto_shutdown?0:1};
    return {};
}
// The EQ a widget should show: a click's target until the speaker confirms it, else the speaker's.
struct ShownEq { int preset=-1; Bands bands{}; };
static ShownEq shown_eq(const Snapshot& state,const WidgetView& view){
    if(view.proposed&&view.proposed->operation==Operation::Preset)return {view.proposed->value,{}};
    if(view.proposed&&view.proposed->operation==Operation::CustomEq)return {tribit::Custom,view.proposed->bands};
    return state.fresh?ShownEq{state.preset,state.bands}:ShownEq{};
}
static int shown_toggle(const WidgetView& view,Operation operation,int index,int value){
    if(view.proposed&&view.proposed->operation==operation&&(operation!=Operation::Prompt||view.proposed->index==index))return view.proposed->value;
    return value;
}
