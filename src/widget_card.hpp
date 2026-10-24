#pragma once
#include "widget_state.hpp"
#include <ctime>

static std::string checked_time(int64_t seconds){
    if(!seconds)return "Not checked yet";
    std::time_t value=seconds;std::tm local{};localtime_s(&local,&value);
    char text[64]{};std::strftime(text,sizeof(text),"Last seen %H:%M",&local);return text;
}
static std::string battery_text(const Snapshot& s){return s.battery>=0?std::to_string(s.battery)+"%":"--";}
static std::string seen_time(int64_t seconds){
    if(!seconds)return {};
    std::time_t value=seconds;std::tm local{};localtime_s(&local,&value);
    char text[16]{};std::strftime(text,sizeof(text),"%H:%M",&local);return text;
}
// One wording for the connection state, used by every page and the companion app.
static std::string status_label(const Snapshot& s){
    if(s.pending)return s.status=="Connecting..."?"Connecting…":"Updating…";
    if(s.ready&&s.fresh)return "Connected";
    return s.error.empty()?"Disconnected":"Controls unavailable";
}
namespace card {
using Items=std::vector<std::string>;
static std::string list(const Items& items){std::string result="[";for(auto& item:items){if(item.empty())continue;if(result.size()>1)result+=",";result+=item;}return result+"]";}
static std::string theme(const std::pair<std::string,std::string>& urls){return "${$host.hostTheme == 'dark' ? '"+urls.second+"' : '"+urls.first+"'}";}
static std::string text(const std::string& value,const char* size="Small",bool subtle=false,bool bold=false,const char* align="Left",int lines=2,const char* color="Default"){
    return "{\"type\":\"TextBlock\",\"spacing\":\"None\",\"text\":"+::quoted(value)+",\"size\":"+::quoted(size)+",\"color\":"+::quoted(color)+",\"isSubtle\":"+(subtle?"true":"false")+",\"weight\":"+::quoted(bold?"Bolder":"Default")+",\"wrap\":true,\"maxLines\":"+std::to_string(lines)+",\"horizontalAlignment\":"+::quoted(align)+"}";
}
static std::string action(const std::string& verb,const std::string& label,bool enabled=true,const std::string& id=""){
    return "{\"type\":\"Action.Execute\",\"id\":"+::quoted(id.empty()?verb:id)+",\"verb\":"+::quoted(verb)+",\"title\":"+::quoted(label)+",\"isEnabled\":"+(enabled?"true":"false")+"}";
}
static std::string box(const Items& items,int height=0,const std::string& click="",const std::string& background="",const char* type="Container"){
    return "{\"type\":"+::quoted(type)+",\"spacing\":\"None\",\"verticalContentAlignment\":\"Center\",\"items\":"+list(items)+(height?",\"minHeight\":"+::quoted(std::to_string(height)+"px"):"")+(click.empty()?"":",\"selectAction\":"+click)+(background.empty()?"":",\"style\":"+::quoted(background))+"}";
}
static std::string col(const Items& items,const std::string& width="stretch",int height=0,const std::string& click="",const std::string& background=""){
    auto result=box(items,height,click,background,"Column");
    return result.substr(0,result.size()-1)+",\"width\":"+::quoted(width)+"}";
}
static std::string columns(const Items& items,int height=0){return "{\"type\":\"ColumnSet\",\"spacing\":\"None\",\"columns\":"+list(items)+(height?",\"minHeight\":"+::quoted(std::to_string(height)+"px"):"")+"}";}
static std::string gap(int height){return box({},height);}
static std::string pad(int width){return col({},std::to_string(width)+"px");}
static std::string image(const std::pair<std::string,std::string>& urls,int width,int height,const std::string& alt=""){
    if(urls.first.empty()||urls.second.empty())return {};
    return "{\"type\":\"Image\",\"spacing\":\"None\",\"url\":"+::quoted(theme(urls))+",\"width\":"+::quoted(std::to_string(width)+"px")+",\"height\":"+::quoted(std::to_string(height)+"px")+",\"horizontalAlignment\":\"Center\",\"altText\":"+::quoted(alt)+"}";
}
static std::string glyph(const WidgetIcons& icons,const char* name,int size=20){auto found=icons.symbols.find(name);return found==icons.symbols.end()?"":image(found->second,size,size);}
// The host's emphasis surface supplies fill, hover, focus and high-contrast
// colours natively. The host adds no inner padding to styled containers, so
// content is inset explicitly to stay inside the tile and its hover highlight.
static std::string tile(const Items& items,int height,const std::string& click,int inset_x=12,int inset_y=8,const char* surface="emphasis"){
    Items inner{inset_x?columns({pad(inset_x),col(items),pad(inset_x)}):columns({col(items)})};
    if(inset_y){inner.insert(inner.begin(),gap(inset_y));inner.push_back(gap(inset_y));}
    return box(inner,height,click,surface);
}
static std::string icon_button(const WidgetIcons& icons,const char* name,const std::string& verb,const std::string& label,bool enabled=true,int size=24){
    return col({glyph(icons,name,18)},std::to_string(size)+"px",size,action(verb,label,enabled));
}
}
static std::string make_card(const Snapshot& s,WidgetSize size,const WidgetView& view={},const WidgetIcons& icons={},bool logging=false){
    using namespace card;
    const bool expanded=size==WidgetSize::Large,enabled=s.ready&&s.fresh&&!s.pending;
    const auto page=view.page;
    Items body;
    auto add=[&](std::string item,int before=0){if(before)body.push_back(gap(before));body.push_back(std::move(item));};
    auto title=[&](const std::string& name,const char* back,const Items& trailing=Items{}){
        Items cols{icon_button(icons,"back",back,"Back",true,24),pad(8),col({text(name,"Default",false,true)},"stretch",24)};
        for(auto& item:trailing)cols.push_back(item);add(columns(cols,24));
    };
    auto page_controls=[&](int page,int pages){
        return Items{col({text(std::to_string(page+1)+" / "+std::to_string(pages),"Small",true)},"auto"),pad(4),
            icon_button(icons,"previous","view.details.previous","Previous page",page>0,24),pad(4),icon_button(icons,"next","view.details.next","Next page",page+1<pages,24)};
    };
    auto nav_tile=[&](const char* symbol,const std::string& title,const std::string& detail,const std::string& verb,const std::string& label,int height){
        Items copy{text(title,"Default",false,true,"Left",1)};if(!detail.empty())copy.push_back(text(detail,"Small",true,false,"Left",1));
        return tile({columns({col({glyph(icons,symbol,20)},"20px"),pad(12),col(copy),col({glyph(icons,"next",16)},"16px")})},height,action(verb,label));
    };
    // Values read before the link dropped stay visible, but always say so.
    const bool stale=!s.fresh&&!s.pending;
    const std::string last_seen=seen_time(s.checked);
    // A click shows its result at once; the speaker's confirmation follows within a fraction of a second.
    const int selected_preset=shown_preset(s,view);
    const bool choosing=view.proposed.has_value()&&s.pending;
    auto preset_tiles=[&](int per_row,int height){
        Items rows;
        for(size_t row=0;row<kPresetOrder.size();row+=per_row){Items chips;
            for(size_t j=row;j<std::min(kPresetOrder.size(),row+per_row);++j){
                // While disconnected the last preset stays bold but dimmed, so no extra note row is needed.
                int id=kPresetOrder[j];bool selected=selected_preset==id,last=stale&&s.preset==id;auto name=preset_label(id);if(j>row)chips.push_back(pad(4));
                // Buttons share the host's neutral surface and hover; only the selected one takes its preset's colour.
                auto fill=selected?icons.fills.find(id):icons.fills.end();bool tinted=fill!=icons.fills.end();
                auto label=text(name,"Small",!enabled&&!choosing&&!selected,selected||last,"Center",2,selected&&!tinted?"Accent":"Default");
                auto button=tile({label},height,action("eq.preset."+std::to_string(id),name+(selected?", selected":last?", last used":""),enabled),4,8);
                if(tinted)button.insert(button.size()-1,",\"backgroundImage\":{\"url\":"+::quoted(theme(fill->second))+",\"fillMode\":\"Repeat\"}");
                chips.push_back(col({button}));
            }
            rows.push_back(gap(row?4:0));rows.push_back(columns(chips,height));
        }
        return box(rows);
    };
    const bool healthy=s.fresh&&s.ready;
    const auto status=status_label(s);
    // Reconnection is automatic; Retry only appears once it has given up with an error.
    const bool retry=!s.ready&&!s.pending&&!s.error.empty();
    auto retry_button=[&](){return col({text("Retry","Small",false,true,"Right",1)},"auto",16,action("retry","Reconnect to the speaker"));};
    if(page==WidgetPage::Home){
        bool valid=s.battery>=0&&s.battery<=100,low=battery_low(s.battery);
        auto described=std::string("Speaker battery ")+(valid?std::to_string(s.battery)+"%":"not reported")+(low?", low":"")+(stale&&valid?", last seen "+last_seen:"");
        auto detail=healthy?std::string():status;
        // One compact line: battery glyph and level, then the connection state when it needs saying.
        // The status shares this line, so nothing below moves when it appears.
        Items line{col({image(icons.battery,28,14,described)},"28px",20),pad(6),
            col({text(valid?std::to_string(s.battery)+"%":"--","Default",!s.fresh,true,"Left",1,low?"Attention":"Default")},"auto",20),
            pad(8),col({text(detail,"Small",true,false,"Left",1)},"stretch",20)};
        if(retry){line.push_back(pad(8));line.push_back(retry_button());}
        if(!expanded){line.push_back(pad(8));line.push_back(icon_button(icons,"settings","view.more","Speaker settings",true,20));}
        add(columns(line,24));
        auto note=s.preset==tribit::Custom&&!stale?std::string("Custom EQ active"):stale&&s.preset>=0?"Last: "+preset_label(s.preset):"";
        if(expanded)add(columns({col({glyph(icons,"equalizer",20)},"20px"),pad(12),col({text("Equalizer","Default",false,true,"Left",1)}),
            col({text(note,"Small",true,false,"Right",1)},"auto")},24),16);
        // The curve follows the highlighted preset; the slot keeps its height when there is none.
        const int curve_height=expanded?kCurveHeights[1]:kCurveHeights[0];
        int curve_preset=selected_preset>=0?selected_preset:s.preset;
        auto curve=icons.curves.find({curve_preset,curve_height});
        if(curve!=icons.curves.end())add(box({image(curve->second,268,curve_height,"Equalizer curve for "+preset_label(curve_preset)+", as drawn in the Tribit app")},curve_height),expanded?8:12);
        else add(box({text(s.preset==tribit::Custom?"Custom EQ: edit it in the Tribit app":"Curve appears once a preset is read","Small",true,false,"Center",2)},curve_height),expanded?8:12);
        add(preset_tiles(3,expanded?60:56),8);
        if(expanded)add(nav_tile("settings","Speaker settings","Voice prompts and auto shutdown","view.more","Open speaker settings",56),16);
    }else if(page==WidgetPage::More||page==WidgetPage::Prompts){
        struct Row{const char* symbol;std::string label,hint;std::string verb;int kind;std::string value;bool on;bool available;}; // kind: 0 link, 1 toggle
        std::vector<Row> rows;
        const bool prompts=page==WidgetPage::Prompts;
        // Speaker-side values are unknown while disconnected; show "--" rather than the last reading.
        auto speaker_value=[&](int value){return value<0||(!s.fresh&&!choosing)?std::string("--"):value?"On":"Off";};
        if(prompts){
            for(int i=0;i<5;++i){int value=shown_toggle(view,Operation::Prompt,i,s.prompts[i]);
                rows.push_back({"bell",kPromptLabels[i],"","prompt."+std::to_string(i),1,speaker_value(value),(s.fresh||choosing)&&value==1,enabled&&s.prompts[i]>=0});}
        }else{
            int shutdown=shown_toggle(view,Operation::AutoShutdown,0,s.auto_shutdown);
            rows.push_back({"bell","Voice prompts","Speaker beeps","view.prompts",0,"",false,true});
            rows.push_back({"power","Auto shutdown","Turns off when idle","pref.autoshutdown",1,speaker_value(shutdown),(s.fresh||choosing)&&shutdown==1,enabled&&s.auto_shutdown>=0});
            rows.push_back({"link","Release for phone app","Frees it when the widget hides","pref.release",1,s.release_when_hidden?"On":"Off",s.release_when_hidden,true});
            rows.push_back({"log","Diagnostic logging","Off unless you need it","pref.logging",1,logging?"On":"Off",logging,true});
            rows.push_back({"diagnostics","Device details","Connection, firmware, Bluetooth","view.diagnostics",0,"",false,true});
        }
        const int per_page=5,pages=(static_cast<int>(rows.size())+per_page-1)/per_page,current=std::clamp(view.details_page,0,pages-1);
        // State notes share the title row, so a disconnected page is no taller than a connected one.
        std::string note=view.failed_write?"Not confirmed":stale?"Not connected":"";
        Items trailing=pages>1?page_controls(current,pages):Items{};
        if(!note.empty())trailing.insert(trailing.begin(),col({text(note,"Small",true,false,"Right",1)},"auto",24));
        title(prompts?"Voice prompts":"Speaker settings",prompts?"view.more":"view.home",trailing);
        for(int i=current*per_page;i<std::min(static_cast<int>(rows.size()),(current+1)*per_page);++i){auto& r=rows[i];
            Items copy{text(r.label,"Small",false,true,"Left",1)};if(!r.hint.empty())copy.push_back(text(r.hint,"Small",true,false,"Left",1));
            Items row{col({glyph(icons,r.symbol,18)},"18px"),pad(12),col(copy,"stretch"),pad(8)};
            // One flat full-row target includes its state or chevron.
            row.push_back(col({r.kind==1?text(r.value,"Small",!r.available&&!choosing,true,"Center",1,r.on?"Accent":"Default"):glyph(icons,"next",16)},r.kind==1?"36px":"20px",28));
            add(tile({columns(row,0)},expanded?52:40,action(r.verb,r.label+(r.kind==1?", "+r.value:""),r.available),12,4),i==current*per_page?8:4);
        }
    }else{
        std::vector<std::pair<std::string,std::string>> rows={{"Model","Tribit XSound Plus 2"},{"Firmware",s.firmware.empty()?"Not reported":s.firmware},
            {"Connection",status},{"Battery",battery_text(s)+(stale&&s.battery>=0?", last known":"")},{"Last seen",last_seen.empty()?"Not yet":last_seen}};
        if(!s.error.empty())rows.emplace_back("Diagnostic",s.error);
        int count=expanded?7:5,pages=(static_cast<int>(rows.size())+1+count-1)/count,current=std::clamp(view.details_page,0,pages-1);
        Items trailing=pages>1?page_controls(current,pages):Items{};
        if(retry){trailing.insert(trailing.begin(),pad(8));trailing.insert(trailing.begin(),retry_button());}
        title("Device","view.more",trailing);
        // The Bluetooth settings link is the last row of the last page.
        for(int i=current*count;i<std::min(static_cast<int>(rows.size())+1,(current+1)*count);++i){
            int before=i==current*count?8:4;
            if(i==static_cast<int>(rows.size())){
                add(tile({columns({col({glyph(icons,"bluetooth",18)},"18px"),pad(12),col({text("Bluetooth settings","Small",false,true,"Left",1)}),col({glyph(icons,"next",16)},"20px")})},40,action("open.bluetooth","Open Windows Bluetooth settings"),12,4),before);
                continue;
            }
            auto& [label,value]=rows[i];
            add(box({columns({col({text(label,"Small",true)}),pad(8),col({text(value,"Small",false,false,"Right",label=="Diagnostic"?4:2)})})},label=="Diagnostic"?0:40),before);
        }
    }
    return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"$schema\":\"http://adaptivecards.io/schemas/adaptive-card.json\",\"body\":"+list(body)+"}";
}
