#include <winsock2.h>
#include <windows.h>
#include <unknwn.h>
#include "controller.hpp"
#include <appmodel.h>
#include "widget_card.hpp"
#include "widget_artwork.hpp"
#include "native_editor.hpp"
#include "webview_host.hpp"
#include "spectrum.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.Windows.Widgets.Providers.h>
#include <winrt/Microsoft.Windows.Widgets.h>
#include <commctrl.h>
#include <shlobj.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <dbt.h>
#include <bthdef.h>

namespace wp=winrt::Microsoft::Windows::Widgets::Providers;
using namespace winrt::Windows::Data::Json;
constexpr GUID ProviderClsid{0xa3fb741b,0xa71f,0x48b5,{0x90,0x4e,0x46,0x05,0xd8,0xd0,0x27,0x72}};
constexpr UINT AppDispatchMessage=WM_APP+1, StateMessage=WM_APP+2;

// Profiles and settings live in %LOCALAPPDATA%\Tribit Connect, outside the package, so uninstalling
// or reinstalling never deletes them (the manifest turns off file-write virtualization, so this is
// the real folder). The first run after the move copies them over from the package's LocalState.
static std::filesystem::path data_folder() {
    PWSTR raw{};winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw));
    std::filesystem::path path(raw);CoTaskMemFree(raw);
    auto folder=path/L"Tribit Connect";std::error_code ec;std::filesystem::create_directories(folder,ec);
    UINT32 length{};GetCurrentPackageFamilyName(&length,nullptr);
    if(length) { std::wstring family(length,L'\0');if(GetCurrentPackageFamilyName(&length,family.data())==ERROR_SUCCESS){family.resize(length-1);
        auto old=path/L"Packages"/family/L"LocalState";
        if(!std::filesystem::exists(folder/L"profiles.json",ec)&&std::filesystem::exists(old/L"profiles.json",ec))
            for(auto name:{L"profiles.json",L"state.json",L"theme"})std::filesystem::copy_file(old/name,folder/name,std::filesystem::copy_options::skip_existing,ec);
    } }
    return folder;
}
static std::string read_file(const std::filesystem::path& path) {
    std::error_code ec;
    if(!std::filesystem::exists(path,ec) || std::filesystem::file_size(path,ec)>65536) return {};
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
template<size_t N> static void read_array(const JsonObject& json,const wchar_t* key,std::array<int,N>& out){
    if(!json.HasKey(key))return;auto values=json.GetNamedArray(key);if(values.Size()!=N)return;
    for(uint32_t i=0;i<N;++i)out[i]=static_cast<int>(values.GetNumberAt(i));
}
template<size_t N> static JsonArray write_array(const std::array<int,N>& values){JsonArray out;for(auto value:values)out.Append(JsonValue::CreateNumberValue(value));return out;}
static Snapshot load_cache(const std::filesystem::path& path,bool& logging) {
    Snapshot s;
    try {
        auto text=read_file(path);if(text.empty())return s;
        auto json=JsonObject::Parse(winrt::to_hstring(text));
        logging=json.GetNamedBoolean(L"logging",false);
        s.release_when_hidden=json.GetNamedBoolean(L"release_when_hidden",false);
        s.address=winrt::to_string(json.GetNamedString(L"address",L""));
        s.firmware=winrt::to_string(json.GetNamedString(L"firmware",L""));
        s.checked=static_cast<int64_t>(json.GetNamedNumber(L"checked",0));
        s.battery=static_cast<int>(json.GetNamedNumber(L"battery",-1));
        s.preset=static_cast<int>(json.GetNamedNumber(L"preset",-1));
        s.auto_shutdown=static_cast<int>(json.GetNamedNumber(L"auto_shutdown",-1));
        read_array(json,L"prompts",s.prompts);read_array(json,L"buttons",s.buttons);
        s.status=s.checked?"Controls idle":"Not connected";
    }catch(...){}
    return s;
}
static std::string icon_data(const std::filesystem::path& path){
    auto bytes=read_file(path);if(bytes.empty())return {};
    return png_uri({reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()});
}
struct WidgetRecord { WidgetSize size=WidgetSize::Medium;bool active{};WidgetView view;std::string last_template,last_data; };
class App;
struct WidgetProvider : winrt::implements<WidgetProvider,wp::IWidgetProvider,wp::IWidgetProvider2> {
    App& app;
    explicit WidgetProvider(App& owner);
    ~WidgetProvider();
    void CreateWidget(wp::WidgetContext context);
    void DeleteWidget(winrt::hstring const& id,winrt::hstring const&);
    void OnActionInvoked(wp::WidgetActionInvokedArgs args);
    void OnWidgetContextChanged(wp::WidgetContextChangedArgs args);
    void OnCustomizationRequested(wp::WidgetCustomizationRequestedArgs args);
    void Activate(wp::WidgetContext context);
    void Deactivate(winrt::hstring id);
};

class App {
public:
    HWND dispatcher{},editor{};
    std::unique_ptr<Controller> controller;
    std::atomic<int> references{};
    std::map<std::wstring,WidgetRecord> widgets;
    WidgetIcons icons;
    WidgetSlots widget_slots=default_slots();     // the Home page's six buttons, chosen in Sound Lab
    std::filesystem::path assets_path;
    bool artwork_loaded{},artwork_contrast{};
    COLORREF artwork_text{};
    HPOWERNOTIFY power_notification{};
    HDEVNOTIFY interface_notification{};
    std::vector<std::pair<HANDLE,HDEVNOTIFY>> radio_notifications;
    bool logging{}, provider_started{}, editor_started{};
    std::filesystem::path folder;
    std::mutex dispatch_mutex;
    std::deque<std::function<void()>> tasks;
    HANDLE stop_pipe=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread pipe_thread;
    std::wstring pipe_name;
    std::unique_ptr<NativeEditor> native;          // fallback when WebView2 is unavailable
    std::unique_ptr<WebViewHost> web;               // the Sound Lab window
    Spectrum spectrum;                              // what this PC plays through the speaker, while the window is open
    HWND dialog{};                                  // native window that needs dialog keyboard handling
    bool web_failed{};
    std::deque<std::pair<Command,Clock::time_point>> waiting; // page writes held while a refresh runs
    App():folder(data_folder()){}
    ~App(){spectrum.stop();web.reset();if(power_notification)UnregisterSuspendResumeNotification(power_notification);if(interface_notification)UnregisterDeviceNotification(interface_notification);for(auto [radio,notice]:radio_notifications){if(notice)UnregisterDeviceNotification(notice);CloseHandle(radio);}SetEvent(stop_pipe);if(pipe_thread.joinable())pipe_thread.join();native.reset();controller.reset();if(dispatcher)DestroyWindow(dispatcher);CloseHandle(stop_pipe);}
    void post(std::function<void()> fn){ {std::lock_guard lock(dispatch_mutex);tasks.push_back(std::move(fn));}PostMessageW(dispatcher,AppDispatchMessage,0,0); }
    void dispatch(){std::deque<std::function<void()>> work;{std::lock_guard lock(dispatch_mutex);work.swap(tasks);}for(auto& fn:work){try{fn();}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}catch(const std::exception& e){diagnostic(e.what());}}}
    void diagnostic(const std::string& text){if(!logging)return;try{auto path=folder/L"diagnostics.log";std::error_code ec;bool reset=std::filesystem::exists(path,ec)&&std::filesystem::file_size(path,ec)>5*1024*1024;
        SYSTEMTIME now{};GetLocalTime(&now);char stamp[32]{};std::snprintf(stamp,sizeof stamp,"%02u:%02u:%02u.%03u",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
        std::ofstream out(path,reset?std::ios::trunc:std::ios::app);out<<stamp<<" "<<text.substr(0,2048)<<"\n";}catch(...){}}
    void init(bool show,int page=0) {
        wchar_t module[32768]{};auto count=GetModuleFileNameW(nullptr,module,32768);
        if(count&&count<32768)assets_path=std::filesystem::path(module).parent_path()/L"Public"/L"Icons";
        WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TribitConnect.Dispatcher";
        RegisterClassW(&wc);dispatcher=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);
        if(!dispatcher)winrt::throw_last_error();
        auto initial=load_cache(folder/L"state.json",logging);
        load_widget_slots(read_library());
        std::filesystem::create_directories(folder);
        controller=std::make_unique<Controller>([this]{PostMessageW(dispatcher,StateMessage,0,0);},std::move(initial));
        // Results reach the page after the state they produced (both travel through the dispatcher).
        controller->on_result=[this](int id,bool ok,const std::string& error){post([this,id,ok,error]{web_result(id,ok,error);});};
        power_notification=RegisterSuspendResumeNotification(dispatcher,DEVICE_NOTIFY_WINDOW_HANDLE);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;filter.dbcc_classguid=GUID_BTHPORT_DEVICE_INTERFACE;
        interface_notification=RegisterDeviceNotificationW(dispatcher,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
        watch_radios();
        if(show)open(page);
        SetTimer(dispatcher,1,30000,nullptr);
    }
    void interest(){bool active=editor!=nullptr;for(auto& [id,widget]:widgets)active|=widget.active;controller->interest(active);}
    void recover(){
        // The board keeps the last card across provider restarts; start from it so an unchanged card is not resent.
        try{for(auto info:wp::WidgetManager::GetDefault().GetWidgetInfos()){auto context=info.WidgetContext();auto& record=widgets[std::wstring(context.Id())];
                record.size=context.Size();record.last_template=winrt::to_string(info.Template());record.last_data=winrt::to_string(info.Data());}
            update_widgets(true);}
        catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    void artwork(const Snapshot& state){
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        bool high=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;auto foreground=GetSysColor(COLOR_WINDOWTEXT);
        if(!artwork_loaded||artwork_contrast!=high||artwork_text!=foreground){
            bool white=GetRValue(foreground)+GetGValue(foreground)+GetBValue(foreground)>384;
            load_artwork(icons,[&](const std::string& file){return icon_data(assets_path/file);},high,white);
            artwork_loaded=true;artwork_contrast=high;artwork_text=foreground;
        }
    }
    // State changes send data only; the template (and with it every image) is resent only when
    // the page, size or artwork changes. Resending it rebuilt the card and made it flash.
    // `force` also updates widgets the board has not activated.
    void update_widgets(bool force=false){
        auto s=controller->snapshot();artwork(s);
        // Saved curves are drawn on demand: the six buttons' and whatever custom curve plays.
        for(auto& slot:widget_slots)if(slot.preset==tribit::Custom)custom_artwork(icons,slot.bands,artwork_contrast);
        if(s.preset==tribit::Custom)custom_artwork(icons,s.bands,artwork_contrast);
        for(auto& [id,record]:widgets){
            reconcile_widget(record.view,s);if(!force&&!record.active)continue;
            try{
                auto card=make_card(s,record.size,record.view,icons,logging,widget_slots);
                bool new_template=card.tmpl!=record.last_template;
                if(!new_template&&card.data==record.last_data)continue;
                wp::WidgetUpdateRequestOptions update(id);
                if(new_template)update.Template(winrt::to_hstring(card.tmpl));
                update.Data(winrt::to_hstring(card.data));
                auto started=Clock::now();
                wp::WidgetManager::GetDefault().UpdateWidget(update);
                if(logging)diagnostic(std::string("update ")+(new_template?"template+data ":"data ")+std::to_string(card.tmpl.size()+card.data.size())+" bytes, "+
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count())+" ms");
                record.last_template=std::move(card.tmpl);record.last_data=std::move(card.data);
            }catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
        }
    }
    void changed(){
        auto state=controller->snapshot();update_widgets();flush_waiting();refresh_editor();
        if(state.checked&&!state.pending)save(state);
        if(!state.error.empty())diagnostic(state.error);
        maybe_exit();
    }
    void save(const Snapshot& s){
        try{JsonObject j;j.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(s.address)));j.SetNamedValue(L"firmware",JsonValue::CreateStringValue(winrt::to_hstring(s.firmware)));
            j.SetNamedValue(L"logging",JsonValue::CreateBooleanValue(logging));j.SetNamedValue(L"checked",JsonValue::CreateNumberValue(static_cast<double>(s.checked)));
            j.SetNamedValue(L"release_when_hidden",JsonValue::CreateBooleanValue(s.release_when_hidden));
            j.SetNamedValue(L"battery",JsonValue::CreateNumberValue(s.battery));j.SetNamedValue(L"preset",JsonValue::CreateNumberValue(s.preset));
            j.SetNamedValue(L"auto_shutdown",JsonValue::CreateNumberValue(s.auto_shutdown));
            j.SetNamedValue(L"prompts",write_array(s.prompts));j.SetNamedValue(L"buttons",write_array(s.buttons));
            auto temporary=folder/L"state.tmp";{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<winrt::to_string(j.Stringify());if(!out)throw std::runtime_error("Could not save settings.");}
            if(!MoveFileExW(temporary.c_str(),(folder/L"state.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();
        }catch(const std::exception& e){diagnostic(e.what());}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    void maybe_exit(){if(!editor && references==0 && (provider_started||editor_started) && !controller->snapshot().ready && !controller->snapshot().pending)PostQuitMessage(0);}
    void release(bool enabled){controller->release_when_hidden(enabled);save(controller->snapshot());}
    void action(const std::wstring& id,const std::wstring& verb){
        auto widget=widgets.find(id);if(widget==widgets.end())return;
        widget->second.active=true;interest();
        auto& view=widget->second.view;
        if(navigate_widget(view,verb)){update_widgets();return;}
        auto state=controller->snapshot();
        if(auto command=setting_command(state,verb,widget_slots)){
            if(controller->send(*command)){view.proposed=*command;view.failed_write=false;update_widgets();}
            return;
        }
        if(verb==L"pref.release"){release(!state.release_when_hidden);update_widgets();return;}
        if(verb==L"pref.logging"){logging=!logging;save(state);update_widgets();return;}
        if(verb==L"open.bluetooth"){ShellExecuteW(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(verb==L"retry")controller->send({Operation::Retry});
    }
    void open(int page){
        editor_started=true;
        if(web&&web->window){web->show();return;}
        if(native&&native->window){native->show(page);return;}
        if(!web_failed){try{open_web();return;}catch(const std::exception& e){diagnostic(e.what());web_failed=true;}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));web_failed=true;}}
        open_native(page);
    }
    void open_native(int page){
        native=std::make_unique<NativeEditor>([this]{return controller->snapshot();},[this](Command command){return controller->send(std::move(command));},
            [this]{editor=dialog=nullptr;interest();post([this]{if(native&&!native->window)native.reset();maybe_exit();});},
            [this]{return logging;},[this](bool enabled){logging=enabled;save(controller->snapshot());},[this](bool enabled){release(enabled);});
        editor=dialog=native->window;native->show(page);interest();
    }
    // ---- Sound Lab (WebView2) ----------------------------------------------------------------
    void open_web(){
        wchar_t module[32768]{};auto count=GetModuleFileNameW(nullptr,module,32768);
        auto content=std::filesystem::path(std::wstring(module,count)).parent_path()/L"app";
        if(!std::filesystem::exists(content/L"index.html"))throw std::runtime_error("The Sound Lab files are missing from the package.");
        // The page's last theme, so the window does not flash the other colour before it paints.
        std::string theme;{std::ifstream in(folder/L"theme",std::ios::binary);std::getline(in,theme);}
        web=std::make_unique<WebViewHost>(content,folder/L"WebView2",theme=="dark");
        web->on_message=[this](const std::wstring& text){web_message(text);};
        web->on_closed=[this]{spectrum.stop();editor=nullptr;waiting.clear();interest();post([this]{if(web&&!web->window)web.reset();maybe_exit();});};
        // No WebView2 runtime (or it failed to start): use the native window instead.
        web->on_failed=[this](HRESULT hr){diagnostic("WebView2 unavailable: "+winrt::to_string(winrt::hresult_error(hr).message()));
            post([this]{web_failed=true;bool showing=web&&web->window;web.reset();if(showing)open_native(0);});};
        spectrum.on_frame=[this](const Spectrum::Levels& levels,bool playing,bool listening){
            JsonObject m;m.SetNamedValue(L"type",JsonValue::CreateStringValue(L"spectrum"));
            m.SetNamedValue(L"playing",JsonValue::CreateBooleanValue(playing));m.SetNamedValue(L"listening",JsonValue::CreateBooleanValue(listening));
            JsonArray bands;for(float v:levels)bands.Append(JsonValue::CreateNumberValue(std::round(v*100)/100));m.SetNamedValue(L"bands",bands);
            auto json=std::wstring(m.Stringify());post([this,json]{if(web&&web->window)web->post(json);});
        };
        spectrum.start();
        editor=web->window;web->show();interest();
    }
    void refresh_editor(){if(native)native->refresh();if(web&&web->window)web->post(state_json());}
    std::filesystem::path library_path()const{return folder/L"profiles.json";}
    static bool read_bands(const JsonObject& o,Bands& out){
        if(!o.HasKey(L"bands"))return false;auto a=o.GetNamedArray(L"bands");if(a.Size()!=out.size())return false;
        for(uint32_t i=0;i<a.Size();++i)out[i]=static_cast<int>(a.GetNumberAt(i));
        return true;
    }
    // Everything the page shows, in the same shape the browser mock sends.
    std::wstring state_json(){
        auto s=controller->snapshot();const bool connected=s.ready&&s.fresh;
        std::wstring status=s.pending&&!connected?L"connecting":connected&&s.pending?L"updating":connected?L"connected":
            (s.status=="Bluetooth unavailable"||!s.error.empty())?L"unavailable":L"disconnected";
        JsonObject speaker;
        speaker.SetNamedValue(L"connected",JsonValue::CreateBooleanValue(connected));
        speaker.SetNamedValue(L"status",JsonValue::CreateStringValue(status));
        speaker.SetNamedValue(L"error",JsonValue::CreateStringValue(winrt::to_hstring(s.error)));
        speaker.SetNamedValue(L"name",JsonValue::CreateStringValue(L"XSound Plus 2"));
        speaker.SetNamedValue(L"firmware",JsonValue::CreateStringValue(winrt::to_hstring(s.firmware)));
        speaker.SetNamedValue(L"battery",JsonValue::CreateNumberValue(s.battery));
        speaker.SetNamedValue(L"preset",s.preset<0?JsonValue::CreateNullValue():JsonValue::CreateNumberValue(s.preset));
        speaker.SetNamedValue(L"bands",write_array(s.bands));
        JsonArray buttons;
        for(size_t i=0;i<s.buttons.size();++i){
            JsonObject b;
            b.SetNamedValue(L"preset",s.buttons[i]<0?JsonValue::CreateNullValue():JsonValue::CreateNumberValue(s.buttons[i]));
            if(s.buttons[i]==tribit::Custom)b.SetNamedValue(L"bands",write_array(s.button_bands[i]));
            buttons.Append(b);
        }
        speaker.SetNamedValue(L"buttons",buttons);
        speaker.SetNamedValue(L"prompts",write_array(s.prompts));
        speaker.SetNamedValue(L"autoShutdown",JsonValue::CreateNumberValue(s.auto_shutdown));
        speaker.SetNamedValue(L"lastSeen",JsonValue::CreateNumberValue(static_cast<double>(s.checked)*1000));
        JsonObject settings;
        settings.SetNamedValue(L"release",JsonValue::CreateBooleanValue(s.release_when_hidden));
        settings.SetNamedValue(L"logging",JsonValue::CreateBooleanValue(logging));
        JsonObject root;
        root.SetNamedValue(L"type",JsonValue::CreateStringValue(L"state"));
        root.SetNamedValue(L"speaker",speaker);root.SetNamedValue(L"settings",settings);
        root.SetNamedValue(L"library",read_library());
        return std::wstring(root.Stringify());
    }
    JsonValue read_library()const{
        try{
            std::error_code ec;auto path=library_path();
            if(std::filesystem::exists(path,ec)&&std::filesystem::file_size(path,ec)<(1u<<20)){
                std::ifstream in(path,std::ios::binary);std::string text{std::istreambuf_iterator<char>(in),{}};
                JsonValue parsed{nullptr};if(JsonValue::TryParse(winrt::to_hstring(text),parsed))return parsed;
            }
        }catch(...){}
        return JsonValue::CreateNullValue();
    }
    // Sound Lab saves the widget's buttons with the library, already resolved to names and curves.
    void load_widget_slots(const JsonValue& library){
        widget_slots=default_slots();
        if(library.ValueType()!=JsonValueType::Object)return;
        auto slots=library.GetObject().GetNamedArray(L"widgetSlots",JsonArray());
        for(uint32_t i=0;i<std::min<uint32_t>(slots.Size(),static_cast<uint32_t>(widget_slots.size()));++i){
            if(slots.GetAt(i).ValueType()!=JsonValueType::Object)continue;
            auto o=slots.GetObjectAt(i);WidgetSlot slot{static_cast<int>(o.GetNamedNumber(L"preset",-1)),{},winrt::to_string(o.GetNamedString(L"name",L""))};
            if(slot.preset==tribit::Custom){if(!read_bands(o,slot.bands)||std::any_of(slot.bands.begin(),slot.bands.end(),[](int b){return b<-8||b>8;}))continue;}
            else if(!valid_preset(slot.preset))continue;
            if(slot.name.empty())slot.name=preset_label(slot.preset);
            widget_slots[i]=std::move(slot);
        }
    }
    void web_result(int id,bool ok,const std::string& error){
        if(!id||!web||!web->window)return;
        JsonObject r;r.SetNamedValue(L"type",JsonValue::CreateStringValue(L"result"));r.SetNamedValue(L"id",JsonValue::CreateNumberValue(id));
        r.SetNamedValue(L"ok",JsonValue::CreateBooleanValue(ok));r.SetNamedValue(L"error",JsonValue::CreateStringValue(winrt::to_hstring(error)));
        web->post(std::wstring(r.Stringify()));
    }
    // A page write that arrives during a background refresh waits a moment instead of failing.
    void submit(Command c){
        if(controller->send(c))return;
        auto s=controller->snapshot();
        if(s.ready&&s.fresh&&s.pending&&waiting.size()<8){waiting.push_back({std::move(c),Clock::now()+4s});return;}
        web_result(c.id,false,s.ready&&s.fresh?"The speaker is busy. Try again.":"The speaker is not connected.");
    }
    void flush_waiting(){
        while(!waiting.empty()){
            auto& [c,deadline]=waiting.front();
            if(Clock::now()>deadline){web_result(c.id,false,"The speaker did not respond in time.");waiting.pop_front();continue;}
            if(!controller->send(c))break;
            waiting.pop_front();
        }
    }
    void save_library(const std::wstring& json){
        try{
            auto temporary=folder/L"profiles.tmp";
            {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<winrt::to_string(json);if(!out)throw std::runtime_error("Could not save profiles.");}
            if(!MoveFileExW(temporary.c_str(),library_path().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();
        }catch(const std::exception& e){diagnostic(e.what());}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    void web_message(const std::wstring& text){
        JsonObject m{nullptr};if(!JsonObject::TryParse(text,m))return;
        const std::wstring type(m.GetNamedString(L"type",L""));const int id=static_cast<int>(m.GetNamedNumber(L"id",0));
        if(type==L"ready"){web->page_ready();web->post(state_json());return;}
        if(type==L"window"){web->command(std::wstring(m.GetNamedString(L"action",L"")));return;}
        if(type==L"apply"){
            Command c{Operation::Preset};c.id=id;const int preset=static_cast<int>(m.GetNamedNumber(L"preset",-1));
            if(preset==tribit::Custom){c.operation=Operation::CustomEq;if(!read_bands(m,c.bands)){web_result(id,false,"No bands were given.");return;}}
            else c.value=preset;
            submit(std::move(c));return;
        }
        if(type==L"assign"){
            Command c{Operation::Buttons};c.id=id;auto slots=m.GetNamedArray(L"slots",JsonArray());
            if(slots.Size()!=3){web_result(id,false,"Three lights are needed.");return;}
            for(uint32_t i=0;i<3;++i){
                auto slot=slots.GetObjectAt(i);c.buttons[i]=static_cast<int>(slot.GetNamedNumber(L"preset",-1));
                if(c.buttons[i]==tribit::Custom&&!read_bands(slot,c.button_bands[i])){web_result(id,false,"No bands were given.");return;}
            }
            submit(std::move(c));return;
        }
        if(type==L"setPrompt"){Command c{Operation::Prompt};c.id=id;c.index=static_cast<int>(m.GetNamedNumber(L"index",-1));c.value=m.GetNamedBoolean(L"value",false)?1:0;submit(std::move(c));return;}
        if(type==L"setAutoShutdown"){Command c{Operation::AutoShutdown};c.id=id;c.value=m.GetNamedBoolean(L"value",false)?1:0;submit(std::move(c));return;}
        if(type==L"setRelease"){release(m.GetNamedBoolean(L"value",false));update_widgets();refresh_editor();return;}
        if(type==L"setLogging"){logging=m.GetNamedBoolean(L"value",false);save(controller->snapshot());update_widgets();refresh_editor();return;}
        if(type==L"saveLibrary"){if(m.HasKey(L"library")){auto library=m.GetNamedValue(L"library");save_library(std::wstring(library.Stringify()));load_widget_slots(library);update_widgets();}return;}
        // Settings > Remove my data: profiles, settings and the log go; the page has already reset itself.
        if(type==L"removeData"){std::error_code ec;
            for(auto name:{L"profiles.json",L"profiles.tmp",L"state.json",L"state.tmp",L"theme",L"diagnostics.log"})std::filesystem::remove(folder/name,ec);
            logging=false;load_widget_slots(JsonValue::CreateNullValue());update_widgets();refresh_editor();return;}
        if(type==L"theme"){const bool dark=m.GetNamedBoolean(L"dark",false);web->theme(dark);
            try{std::ofstream(folder/L"theme",std::ios::binary|std::ios::trunc)<<(dark?"dark":"light");}catch(...){}return;}
        if(type==L"retry"){controller->send({Operation::Retry});return;}
        if(type==L"openBluetooth"){ShellExecuteW(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;}
    }
    void watch_radios(){
        for(auto [radio,notice]:radio_notifications){if(notice)UnregisterDeviceNotification(notice);CloseHandle(radio);}radio_notifications.clear();
        BLUETOOTH_FIND_RADIO_PARAMS search{sizeof(search)};HANDLE radio{};
        auto found=BluetoothFindFirstRadio(&search,&radio);if(!found)return;
        do{DEV_BROADCAST_HANDLE filter{};filter.dbch_size=sizeof(filter);filter.dbch_devicetype=DBT_DEVTYP_HANDLE;filter.dbch_handle=radio;auto notice=RegisterDeviceNotificationW(dispatcher,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);radio_notifications.emplace_back(radio,notice);}while(BluetoothFindNextRadio(found,&radio));
        BluetoothFindRadioClose(found);
    }
    bool remembered(BTH_ADDR value){return controller->snapshot().address==tribit::format_address(value);}
    void device_event(WPARAM event,LPARAM data){
        if(!data)return;auto* header=reinterpret_cast<DEV_BROADCAST_HDR*>(data);
        if(event==DBT_CUSTOMEVENT&&header->dbch_devicetype==DBT_DEVTYP_HANDLE){
            auto* notification=reinterpret_cast<DEV_BROADCAST_HANDLE*>(data);
            if(header->dbch_size<offsetof(DEV_BROADCAST_HANDLE,dbch_data)+sizeof(BTH_HCI_EVENT_INFO))return;
            if(notification->dbch_eventguid==GUID_BLUETOOTH_HCI_EVENT){BTH_HCI_EVENT_INFO info{};std::memcpy(&info,notification->dbch_data,sizeof(info));if(info.connectionType==HCI_CONNECTION_TYPE_ACL&&remembered(info.bthAddress))controller->lifecycle(info.connected?Operation::Reconnect:Operation::Disconnect);}
        }else if(header->dbch_devicetype==DBT_DEVTYP_DEVICEINTERFACE&&(event==DBT_DEVICEARRIVAL||event==DBT_DEVICEREMOVECOMPLETE)){
            if(event==DBT_DEVICEREMOVECOMPLETE)controller->lifecycle(Operation::Disconnect);
            watch_radios();
        }
    }
    void start_pipe(PSECURITY_DESCRIPTOR descriptor){
        pipe_thread=std::thread([this,descriptor]{
            SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
            for(;;){
                HANDLE pipe=CreateNamedPipeW(pipe_name.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,64,64,0,&attributes);
                if(pipe==INVALID_HANDLE_VALUE)return;
                HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);OVERLAPPED ov{};ov.hEvent=event;
                BOOL connected=ConnectNamedPipe(pipe,&ov);DWORD error=connected?ERROR_SUCCESS:GetLastError();
                HANDLE waits[]={stop_pipe,event};
                bool ready=connected||error==ERROR_PIPE_CONNECTED;
                if(error==ERROR_IO_PENDING)ready=WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1;
                if(ready){ResetEvent(event);DWORD page{},bytes{};BOOL read=ReadFile(pipe,&page,sizeof(page),&bytes,&ov);if(!read&&GetLastError()==ERROR_IO_PENDING&&WaitForMultipleObjects(2,waits,FALSE,1000)==WAIT_OBJECT_0+1)read=GetOverlappedResult(pipe,&ov,&bytes,FALSE);
                    if(read&&bytes==sizeof(page)&&page<=2)post([this,page]{open(static_cast<int>(page));});}
                CancelIoEx(pipe,nullptr);DisconnectNamedPipe(pipe);CloseHandle(pipe);CloseHandle(event);
                if(WaitForSingleObject(stop_pipe,0)==WAIT_OBJECT_0)return;
            }
        });
    }
    static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wparam,lparam);
        try{
            if(message==AppDispatchMessage){self->dispatch();return 0;}
            if(message==StateMessage){self->changed();return 0;}
            if(message==WM_DEVICECHANGE){self->device_event(wparam,lparam);return TRUE;}
            if(message==WM_POWERBROADCAST){if(wparam==PBT_APMSUSPEND)self->controller->lifecycle(Operation::Suspend);else if(wparam==PBT_APMRESUMEAUTOMATIC||wparam==PBT_APMRESUMESUSPEND)self->controller->lifecycle(Operation::Reconnect);return TRUE;}
            if(message==WM_TIMER&&window==self->dispatcher){KillTimer(window,1);self->maybe_exit();return 0;}
        }catch(const winrt::hresult_error& e){self->diagnostic(winrt::to_string(e.message()));}catch(const std::exception& e){self->diagnostic(e.what());}
        return DefWindowProcW(window,message,wparam,lparam);
    }
};

WidgetProvider::WidgetProvider(App& owner):app(owner){++app.references;app.post([&owner]{owner.provider_started=true;owner.recover();});}
WidgetProvider::~WidgetProvider(){--app.references;app.post([owner=&app]{owner->maybe_exit();});}
void WidgetProvider::CreateWidget(wp::WidgetContext context){auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{owner->widgets[id]={size,false};owner->update_widgets();});}
void WidgetProvider::DeleteWidget(winrt::hstring const& id,winrt::hstring const&){app.post([owner=&app,id=std::wstring(id)]{owner->widgets.erase(id);owner->interest();});}
void WidgetProvider::OnActionInvoked(wp::WidgetActionInvokedArgs args){auto id=std::wstring(args.WidgetContext().Id());auto verb=std::wstring(args.Verb());
    app.post([owner=&app,id,verb]{owner->diagnostic("click "+winrt::to_string(verb));owner->action(id,verb);});}
void WidgetProvider::OnWidgetContextChanged(wp::WidgetContextChangedArgs args){auto context=args.WidgetContext();auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{owner->widgets[id].size=size;owner->update_widgets();});}
void WidgetProvider::OnCustomizationRequested(wp::WidgetCustomizationRequestedArgs args){auto context=args.WidgetContext();auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{auto& widget=owner->widgets[id];widget.size=size;widget.active=true;widget.view.page=WidgetPage::More;widget.view.details_page=0;owner->interest();owner->update_widgets();});}
void WidgetProvider::Activate(wp::WidgetContext context){auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{auto& widget=owner->widgets[id];widget.size=size;widget.active=true;owner->interest();owner->update_widgets();});}
void WidgetProvider::Deactivate(winrt::hstring id){app.post([owner=&app,id=std::wstring(id)]{auto found=owner->widgets.find(id);if(found!=owner->widgets.end())found->second.active=false;owner->interest();});}

struct ProviderFactory:winrt::implements<ProviderFactory,IClassFactory>{
    App& app;explicit ProviderFactory(App& owner):app(owner){}
    HRESULT __stdcall CreateInstance(IUnknown* outer,REFIID iid,void** value) noexcept override{if(!value)return E_POINTER;*value=nullptr;if(outer)return CLASS_E_NOAGGREGATION;try{return winrt::make<WidgetProvider>(app).as(iid,value);}catch(...){return winrt::to_hresult();}}
    HRESULT __stdcall LockServer(BOOL lock) noexcept override{if(lock)++app.references;else --app.references;return S_OK;}
};

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR arguments,int){
    bool provider=wcsstr(arguments,L"--provider")||wcsstr(arguments,L"-Embedding");
    DWORD start_page=wcsstr(arguments,L"--settings")?2:wcsstr(arguments,L"--device")?1:0;
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        HANDLE token{};winrt::check_bool(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));DWORD bytes{};GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> buffer(bytes);winrt::check_bool(GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes));CloseHandle(token);
        LPWSTR raw_sid{};winrt::check_bool(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&raw_sid));std::wstring sid(raw_sid);LocalFree(raw_sid);
        PSECURITY_DESCRIPTOR descriptor{};winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptorW((L"D:P(A;;GA;;;"+sid+L")").c_str(),SDDL_REVISION_1,&descriptor,nullptr));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};DWORD session_id{};ProcessIdToSessionId(GetCurrentProcessId(),&session_id);
        auto suffix=sid+L"."+std::to_wstring(session_id);auto pipe=L"\\\\.\\pipe\\TribitConnect."+suffix;
        HANDLE mutex=CreateMutexW(&attributes,FALSE,(L"Local\\TribitConnect."+suffix).c_str());if(!mutex)winrt::throw_last_error();
        if(GetLastError()==ERROR_ALREADY_EXISTS){
            if(!provider){auto deadline=Clock::now()+2s;while(Clock::now()<deadline){HANDLE client=CreateFileW(pipe.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(client!=INVALID_HANDLE_VALUE){DWORD page=start_page,written{};WriteFile(client,&page,sizeof(page),&written,nullptr);CloseHandle(client);break;}std::this_thread::sleep_for(50ms);}}
            LocalFree(descriptor);CloseHandle(mutex);return 0;
        }
        {
            App app;app.pipe_name=pipe;app.init(!provider,static_cast<int>(start_page));app.start_pipe(descriptor);
            auto factory=winrt::make<ProviderFactory>(app);DWORD cookie{};
            winrt::check_hresult(CoRegisterClassObject(ProviderClsid,factory.get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie));
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(app.dialog&&IsDialogMessageW(app.dialog,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
            CoRevokeClassObject(cookie);
        }
        LocalFree(descriptor);CloseHandle(mutex);return 0;
    }catch(const winrt::hresult_error& e){if(!provider)MessageBoxW(nullptr,e.message().c_str(),L"Tribit Connect",MB_ICONERROR);return 1;}
    catch(const std::exception& e){if(!provider)MessageBoxW(nullptr,winrt::to_hstring(e.what()).c_str(),L"Tribit Connect",MB_ICONERROR);return 1;}
}
