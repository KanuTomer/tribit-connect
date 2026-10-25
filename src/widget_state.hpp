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
};
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
static std::optional<Command> setting_command(const Snapshot& state,std::wstring_view verb){
    if(!state.ready||!state.fresh||state.pending)return {};
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
// The value a widget should show right after a click, before the speaker confirms it.
static int shown_preset(const Snapshot& state,const WidgetView& view){
    if(view.proposed&&view.proposed->operation==Operation::Preset)return view.proposed->value;
    return state.fresh?state.preset:-1;
}
static int shown_toggle(const WidgetView& view,Operation operation,int index,int value){
    if(view.proposed&&view.proposed->operation==operation&&(operation!=Operation::Prompt||view.proposed->index==index))return view.proposed->value;
    return value;
}
