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
    if(s.pending&&!(s.ready&&s.fresh))return s.status=="Connecting..."?"Connecting…":"Updating…";
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
// Bound values: ${name} in the template is replaced from the data object by the host.
static std::string bound(const std::string& expr){return ::quoted("${"+expr+"}");}
// Hides an element unless a data field is true; hidden elements take no space.
static std::string shown_if(std::string element,const std::string& expr){
    if(element.empty())return element;
    element.insert(element.size()-1,",\"isVisible\":"+bound(expr));return element;
}
// A text block whose properties may be bound: pass already-encoded JSON values.
static std::string text_json(const std::string& value,const char* size,const std::string& subtle,const std::string& weight,const char* align,int lines,const std::string& color){
    return "{\"type\":\"TextBlock\",\"spacing\":\"None\",\"text\":"+value+",\"size\":"+::quoted(size)+",\"color\":"+color+",\"isSubtle\":"+subtle+",\"weight\":"+weight+",\"wrap\":true,\"maxLines\":"+std::to_string(lines)+",\"horizontalAlignment\":"+::quoted(align)+"}";
}
static std::string action_json(const std::string& verb,const std::string& title,const std::string& enabled){
    return "{\"type\":\"Action.Execute\",\"id\":"+::quoted(verb)+",\"verb\":"+::quoted(verb)+",\"title\":"+title+",\"isEnabled\":"+enabled+"}";
}
// An image whose light and dark URLs come from data fields.
static std::string bound_image(const std::string& field,int width,int height,const std::string& alt){
    return "{\"type\":\"Image\",\"spacing\":\"None\",\"url\":"+bound("$host.hostTheme == 'dark' ? "+field+".dark : "+field+".light")+",\"width\":"+::quoted(std::to_string(width)+"px")+",\"height\":"+::quoted(std::to_string(height)+"px")+",\"horizontalAlignment\":\"Center\",\"altText\":"+alt+"}";
}
}

// A widget card is a template plus data. The template depends only on size, page and
// artwork; everything about the speaker's state lives in the data. Updates that only
// change state send new data with the same template, so the host keeps the card and
// its images instead of rebuilding it, which made every change flash.
struct WidgetCard{std::string tmpl,data;};

static std::string card_template(WidgetSize size,const WidgetView& view={},const WidgetIcons& icons={}){
    using namespace card;
    const bool expanded=size==WidgetSize::Large;
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
    auto note_text=[&](const char* field,const char* align){return text_json(bound(field),"Small","true",::quoted("Default"),align,1,::quoted("Default"));};
    auto retry_button=[&](){return shown_if(col({text("Retry","Small",false,true,"Right",1)},"auto",16,action("retry","Reconnect to the speaker")),"showRetry");};
    if(page==WidgetPage::Home){
        // One compact line: battery bar and level, then the connection state when it needs saying.
        // The bar is native containers rather than an image: the host rebuilds the card on every
        // update, and images blank out while they reload, which read as the whole widget flashing.
        auto bar=box({columns({col({},"${batteryFill}",10,"","${batteryStyle}"),col({},"stretch",10,"","emphasis")},10)},20);
        Items line{col({bar},"28px",20),pad(6),
            col({text_json(bound("batteryText"),"Default",bound("batterySubtle"),::quoted("Bolder"),"Left",1,bound("batteryColor"))},"auto",20),
            pad(8),col({note_text("status","Left")},"stretch",20),retry_button()};
        // On medium the row sits 4px lower, clear of the board's title bar, which took clicks on the gear's top edge.
        if(!expanded){line.push_back(pad(8));line.push_back(icon_button(icons,"settings","view.more","Speaker settings",true,20));}
        add(columns(line,24),expanded?0:4);
        if(expanded)add(columns({col({glyph(icons,"equalizer",20)},"20px"),pad(12),col({text("Equalizer","Default",false,true,"Left",1)}),
            col({note_text("eqNote","Right")},"auto")},24),16);
        // One curve image, chosen by the data; the slot keeps its height when there is none.
        const int curve_height=expanded?kCurveHeights[1]:kCurveHeights[0];
        add(box({shown_if(bound_image("curve",268,curve_height,bound("curveAlt")),"hasCurve"),
            shown_if(box({text_json(bound("curveNote"),"Small","true",::quoted("Default"),"Center",2,::quoted("Default"))},curve_height),"!hasCurve")},curve_height),expanded?8:12);
        // As in Sound Connect: every button uses the host's neutral surface and hover, and the
        // selected one is marked by bold accent text. No images, so a switch redraws quickly.
        const int per_row=3,height=expanded?60:56;Items rows;
        for(size_t row=0;row<kPresetOrder.size();row+=per_row){Items chips;
            for(size_t j=row;j<std::min(kPresetOrder.size(),row+per_row);++j){
                int id=kPresetOrder[j];auto p="p"+std::to_string(id);if(j>row)chips.push_back(pad(4));
                auto label=text_json(::quoted(preset_label(id)),"Small",bound(p+".subtle"),bound(p+".weight"),"Center",2,bound(p+".color"));
                chips.push_back(col({tile({label},height,action_json("eq.preset."+std::to_string(id),bound(p+".title"),bound("enabled")),4,8)}));
            }
            rows.push_back(gap(row?4:0));rows.push_back(columns(chips,height));
        }
        add(box(rows),8);
        if(expanded)add(nav_tile("settings","Speaker settings","Voice prompts and auto shutdown","view.more","Open speaker settings",56),16);
    }else if(page==WidgetPage::More||page==WidgetPage::Prompts){
        struct Row{const char* symbol;std::string label,hint,verb;bool toggle;};
        std::vector<Row> rows;
        const bool prompts=page==WidgetPage::Prompts;
        if(prompts)for(int i=0;i<5;++i)rows.push_back({"bell",kPromptLabels[i],"","prompt."+std::to_string(i),true});
        else rows={{"bell","Voice prompts","Speaker beeps","view.prompts",false},{"power","Auto shutdown","Turns off when idle","pref.autoshutdown",true},
            {"link","Release for phone app","Frees it when the widget hides","pref.release",true},{"log","Diagnostic logging","Off unless you need it","pref.logging",true},
            {"diagnostics","Device details","Connection, firmware, Bluetooth","view.diagnostics",false}};
        const int per_page=5,pages=(static_cast<int>(rows.size())+per_page-1)/per_page,current=std::clamp(view.details_page,0,pages-1);
        // State notes share the title row, so a disconnected page is no taller than a connected one.
        Items trailing{col({note_text("note","Right")},"auto",24)};
        if(pages>1)for(auto& item:page_controls(current,pages))trailing.push_back(item);
        title(prompts?"Voice prompts":"Speaker settings",prompts?"view.more":"view.home",trailing);
        for(int i=current*per_page;i<std::min(static_cast<int>(rows.size()),(current+1)*per_page);++i){auto& r=rows[i];
            auto field=std::string(prompts?"q":"m")+std::to_string(i);
            Items copy{text(r.label,"Small",false,true,"Left",1)};if(!r.hint.empty())copy.push_back(text(r.hint,"Small",true,false,"Left",1));
            Items row{col({glyph(icons,r.symbol,18)},"18px"),pad(12),col(copy,"stretch"),pad(8)};
            // One flat full-row target includes its state or chevron.
            row.push_back(col({r.toggle?text_json(bound(field+".value"),"Small",bound(field+".subtle"),::quoted("Bolder"),"Center",1,bound(field+".color")):glyph(icons,"next",16)},r.toggle?"36px":"20px",28));
            auto click=r.toggle?action_json(r.verb,bound(field+".title"),bound(field+".enabled")):action(r.verb,r.label);
            add(tile({columns(row,0)},expanded?52:40,click,12,4),i==current*per_page?8:4);
        }
    }else{
        // The diagnostic row is always in the template and only shows when there is an error.
        struct Row{const char* label;const char* field;};
        const Row rows[]={{"Model","model"},{"Firmware","firmware"},{"Connection","connection"},{"Battery","batteryDetail"},{"Last seen","lastSeen"},{"Diagnostic","error"}};
        const int total=static_cast<int>(std::size(rows))+1,count=expanded?7:5,pages=(total+count-1)/count,current=std::clamp(view.details_page,0,pages-1);
        Items trailing{retry_button()};
        if(pages>1)for(auto& item:page_controls(current,pages))trailing.push_back(item);
        title("Device","view.more",trailing);
        // The Bluetooth settings link is the last row of the last page.
        for(int i=current*count;i<std::min(total,(current+1)*count);++i){
            int before=i==current*count?8:4;
            if(i==total-1){
                add(tile({columns({col({glyph(icons,"bluetooth",18)},"18px"),pad(12),col({text("Bluetooth settings","Small",false,true,"Left",1)}),col({glyph(icons,"next",16)},"20px")})},40,action("open.bluetooth","Open Windows Bluetooth settings"),12,4),before);
                continue;
            }
            bool diagnostic=std::string_view(rows[i].field)=="error";
            auto row=box({columns({col({text(rows[i].label,"Small",true)}),pad(8),col({text_json(bound(rows[i].field),"Small","false",::quoted("Default"),"Right",diagnostic?4:2,::quoted("Default"))})})},diagnostic?0:40);
            if(diagnostic){body.push_back(shown_if(box({gap(before),row}),"hasError"));continue;}
            add(row,before);
        }
    }
    return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"$schema\":\"http://adaptivecards.io/schemas/adaptive-card.json\",\"body\":"+card::list(body)+"}";
}

// Everything the template binds, for the current state. One object serves every page.
static std::string card_data(const Snapshot& s,WidgetSize size,const WidgetView& view={},const WidgetIcons& icons={},bool logging=false){
    // A background refresh on a working link changes nothing on the card: every update rebuilds it.
    const bool enabled=s.ready&&s.fresh,healthy=s.fresh&&s.ready;
    const auto page=view.page;
    // Values read before the link dropped stay visible, but always say so.
    const bool stale=!s.fresh&&!s.pending;
    const std::string last_seen=seen_time(s.checked);
    // A click shows its result at once; the speaker's confirmation follows within a fraction of a second.
    const int selected_preset=shown_preset(s,view);
    const bool choosing=view.proposed.has_value()&&s.pending;
    bool valid=s.battery>=0&&s.battery<=100,low=battery_low(s.battery);
    std::vector<std::pair<std::string,std::string>> fields; // name -> encoded JSON value
    auto str=[&](const std::string& name,const std::string& value){fields.emplace_back(name,::quoted(value));};
    auto flag=[&](const std::string& name,bool value){fields.emplace_back(name,value?"true":"false");};
    auto obj=[&](const std::string& name,const std::vector<std::pair<std::string,std::string>>& members){
        std::string out="{";for(auto& [k,v]:members){if(out.size()>1)out+=",";out+=::quoted(k)+":"+v;}fields.emplace_back(name,out+"}");};
    // While a click waits for the speaker, the data already shows the expected result, so the
    // confirmation produces identical data and sends no second update (each update rebuilds the card).
    const bool live=enabled||choosing;
    flag("enabled",live);
    flag("showRetry",!s.ready&&!s.pending&&!s.error.empty());
    // Only the fields the current page binds, so values it doesn't show (the last-seen
    // minute, the curve on the settings pages) never trigger an update.
    if(page==WidgetPage::Home){
    str("batteryFill",valid?std::to_string(std::max(2,(s.battery*22+50)/100))+"px":"1px");
    str("batteryStyle",!valid?"emphasis":low?"attention":"good");
    str("batteryText",valid?std::to_string(s.battery)+"%":"--");
    flag("batterySubtle",!s.fresh);
    str("batteryColor",low?"Attention":"Default");
    str("status",healthy?std::string():status_label(s));
    str("eqNote",s.preset==tribit::Custom&&!stale?std::string("Custom EQ active"):stale&&s.preset>=0?"Last: "+preset_label(s.preset):"");
    // The curve follows the highlighted preset; while disconnected it shows the last one.
    int curve=selected_preset>=0?selected_preset:s.preset;
    auto art=icons.curves.find({curve,size==WidgetSize::Large?kCurveHeights[1]:kCurveHeights[0]});
    bool has_curve=valid_preset(curve)&&art!=icons.curves.end();
    flag("hasCurve",has_curve);
    obj("curve",{{"light",::quoted(has_curve?art->second.first:"")},{"dark",::quoted(has_curve?art->second.second:"")}});
    str("curveAlt",has_curve?"Equalizer curve for "+preset_label(curve)+", as drawn in the Tribit app":"");
    str("curveNote",s.preset==tribit::Custom?"Custom EQ: edit it in the Tribit app":"Curve appears once a preset is read");
    for(int id:kPresetOrder){
        bool selected=selected_preset==id,last=stale&&s.preset==id;
        obj("p"+std::to_string(id),{{"color",::quoted(selected?"Accent":"Default")},
            {"weight",::quoted(selected||last?"Bolder":"Default")},{"subtle",!live&&!selected?"true":"false"},
            {"title",::quoted(preset_label(id)+(selected?", selected":last?", last used":""))}});
    }
    }else if(page==WidgetPage::More||page==WidgetPage::Prompts){
    // Speaker-side values are unknown while disconnected; show "--" rather than the last reading.
    auto toggle=[&](const std::string& name,const std::string& label,int value,bool speaker,bool available){
        bool known=value>=0&&(!speaker||s.fresh||choosing),on=known&&value==1;
        auto text=known?(value?"On":"Off"):std::string("--");
        obj(name,{{"value",::quoted(text)},{"color",::quoted(on?"Accent":"Default")},{"subtle",!available?"true":"false"},
            {"title",::quoted(label+", "+text)},{"enabled",available?"true":"false"}});
    };
    for(int i=0;i<5;++i)toggle("q"+std::to_string(i),kPromptLabels[i],shown_toggle(view,Operation::Prompt,i,s.prompts[i]),true,live&&s.prompts[i]>=0);
    toggle("m1","Auto shutdown",shown_toggle(view,Operation::AutoShutdown,0,s.auto_shutdown),true,live&&s.auto_shutdown>=0);
    toggle("m2","Release for phone app",s.release_when_hidden?1:0,false,true);
    toggle("m3","Diagnostic logging",logging?1:0,false,true);
    str("note",view.failed_write?"Not confirmed":stale?"Not connected":"");
    }else{
    str("model","Tribit XSound Plus 2");
    str("firmware",s.firmware.empty()?"Not reported":s.firmware);
    str("batteryDetail",battery_text(s)+(stale&&s.battery>=0?", last known":""));
    str("lastSeen",last_seen.empty()?"Not yet":last_seen);
    str("error",s.error);flag("hasError",!s.error.empty());
    // The Device page's Connection row reads the full label even when Home hides it.
    str("connection",choosing?"Connected":status_label(s));
    }
    std::string out="{";for(auto& [k,v]:fields){if(out.size()>1)out+=",";out+=::quoted(k)+":"+v;}return out+"}";
}
static WidgetCard make_card(const Snapshot& s,WidgetSize size,const WidgetView& view={},const WidgetIcons& icons={},bool logging=false){
    return {card_template(size,view,icons),card_data(s,size,view,icons,logging)};
}
