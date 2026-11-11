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
#include "updates.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Microsoft.Windows.Widgets.Providers.h>
#include <winrt/Microsoft.Windows.Widgets.h>
#include <commctrl.h>
#include <shlobj.h>
#include <sddl.h>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <dbt.h>
#include <bthdef.h>

namespace wp=winrt::Microsoft::Windows::Widgets::Providers;
using namespace winrt::Windows::Data::Json;
constexpr GUID ProviderClsid{0xa3fb741b,0xa71f,0x48b5,{0x90,0x4e,0x46,0x05,0xd8,0xd0,0x27,0x72}};
constexpr UINT AppDispatchMessage=WM_APP+1, StateMessage=WM_APP+2, TrayMessage=WM_APP+3;
constexpr wchar_t StartupTaskId[]=L"TribitConnectStartup";   // packaging/AppxManifest.xml

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
// What a speaker last reported, as kept in state.json (flat for the speaker in use, and per address
// under "speakers" so switching back shows that speaker's own last-known values).
static void read_speaker(const JsonObject& json,Snapshot& s){
    s.firmware=winrt::to_string(json.GetNamedString(L"firmware",L""));
    s.checked=static_cast<int64_t>(json.GetNamedNumber(L"checked",0));
    s.battery=static_cast<int>(json.GetNamedNumber(L"battery",-1));
    s.preset=static_cast<int>(json.GetNamedNumber(L"preset",-1));
    s.auto_shutdown=static_cast<int>(json.GetNamedNumber(L"auto_shutdown",-1));
    read_array(json,L"prompts",s.prompts);read_array(json,L"buttons",s.buttons);read_array(json,L"bands",s.bands);
    // "Let the phone app connect" belongs to the speaker (0.7.1); an entry from before keeps the current value.
    if(json.HasKey(L"release_when_hidden"))s.release_when_hidden=json.GetNamedBoolean(L"release_when_hidden",false);
}
static void write_speaker(JsonObject& j,const Snapshot& s){
    j.SetNamedValue(L"firmware",JsonValue::CreateStringValue(winrt::to_hstring(s.firmware)));
    j.SetNamedValue(L"checked",JsonValue::CreateNumberValue(static_cast<double>(s.checked)));
    j.SetNamedValue(L"battery",JsonValue::CreateNumberValue(s.battery));j.SetNamedValue(L"preset",JsonValue::CreateNumberValue(s.preset));
    j.SetNamedValue(L"auto_shutdown",JsonValue::CreateNumberValue(s.auto_shutdown));
    j.SetNamedValue(L"prompts",write_array(s.prompts));j.SetNamedValue(L"buttons",write_array(s.buttons));j.SetNamedValue(L"bands",write_array(s.bands));
    j.SetNamedValue(L"release_when_hidden",JsonValue::CreateBooleanValue(s.release_when_hidden));
}
struct Cache{Snapshot initial;bool logging{},chosen{},check_updates=true,tray{};int64_t last_update_check{};JsonObject speakers{nullptr};};
static Cache load_cache(const std::filesystem::path& path) {
    Cache c;Snapshot& s=c.initial;c.speakers=JsonObject();
    try {
        auto text=read_file(path);if(text.empty())return c;
        auto json=JsonObject::Parse(winrt::to_hstring(text));
        c.logging=json.GetNamedBoolean(L"logging",false);
        c.chosen=json.GetNamedBoolean(L"chosen",false);
        c.check_updates=json.GetNamedBoolean(L"check_updates",true);
        c.tray=json.GetNamedBoolean(L"tray",false);
        c.last_update_check=static_cast<int64_t>(json.GetNamedNumber(L"last_update_check",0));
        s.release_when_hidden=json.GetNamedBoolean(L"release_when_hidden",false);
        s.address=winrt::to_string(json.GetNamedString(L"address",L""));
        read_speaker(json,s);
        s.status=s.checked?"Controls idle":"Not connected";
        if(json.HasKey(L"speakers")&&json.GetNamedValue(L"speakers").ValueType()==JsonValueType::Object)c.speakers=json.GetNamedObject(L"speakers");
        // 0.6 and older kept only the flat fields: they become this speaker's entry.
        if(!s.address.empty()&&!c.speakers.HasKey(winrt::to_hstring(s.address))){JsonObject one;write_speaker(one,s);c.speakers.SetNamedValue(winrt::to_hstring(s.address),one);}
    }catch(...){}
    return c;
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
    WidgetSpeakers widget_speakers;               // paired speakers this app controls (refresh_speakers)
    std::filesystem::path assets_path;
    bool artwork_loaded{},artwork_contrast{};
    COLORREF artwork_text{};
    HPOWERNOTIFY power_notification{};
    HDEVNOTIFY interface_notification{};
    std::vector<std::pair<HANDLE,HDEVNOTIFY>> radio_notifications;
    bool logging{}, provider_started{}, editor_started{};
    bool chosen{};                                  // asked once which of two speakers to use
    bool check_updates=true;int64_t last_update_check{};
    bool tray{};                                    // Close to tray: closing the window hides it, the icon stays
    bool quitting{};                                // Quit from the tray menu: really close
    bool start_hidden{};                            // launched by the startup task at sign-in: start in the tray
    HWND tray_window{};bool in_tray{};
    std::wstring startup=L"unknown";                // Start with Windows: on|off|user-off|policy-on|policy-off|unavailable
    std::wstring update_state=L"idle",update_version; // idle|checking|current|available|failed|dev|downloading|unverified|installing
    std::optional<tribit::updates::Release> available;
    static std::wstring app_version(){auto v=tribit::updates::app_version();return v.empty()?L"Development build":v;}
    JsonObject memory{nullptr};                     // last-known values per speaker address (state.json "speakers")
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
    // Worker threads (update check, download, startup task) check this before posting back.
    std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
    bool library_error{},library_backed_up{};       // profiles.json could not be read; its start-of-session copy was made
    Clock::time_point tray_until{};                 // the tray menu was used: the speaker's controls stay open until then
    int tray_wanted=-1;Clock::time_point tray_wanted_until{};   // a tray button picked before the speaker linked
    DWORD tray_menu_closed{};                       // tick when the tray menu last closed (one click can arrive twice)
    App():folder(data_folder()){}
    ~App(){*alive=false;spectrum.stop();web.reset();if(in_tray){auto data=tray_data();Shell_NotifyIconW(NIM_DELETE,&data);}if(tray_window)DestroyWindow(tray_window);if(power_notification)UnregisterSuspendResumeNotification(power_notification);if(interface_notification)UnregisterDeviceNotification(interface_notification);for(auto [radio,notice]:radio_notifications){if(notice)UnregisterDeviceNotification(notice);CloseHandle(radio);}SetEvent(stop_pipe);if(pipe_thread.joinable())pipe_thread.join();native.reset();controller.reset();if(dispatcher)DestroyWindow(dispatcher);CloseHandle(stop_pipe);}
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
        // An ordinary (never shown) window for the tray icon: message-only windows miss TaskbarCreated
        // and cannot own the icon's menu.
        WNDCLASSW tc{};tc.lpfnWndProc=window_proc;tc.hInstance=wc.hInstance;tc.lpszClassName=L"TribitConnect.Tray";RegisterClassW(&tc);
        tray_window=CreateWindowExW(WS_EX_TOOLWINDOW,tc.lpszClassName,L"Tribit Connect",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);
        auto cache=load_cache(folder/L"state.json");
        logging=cache.logging;chosen=cache.chosen;check_updates=cache.check_updates;tray=cache.tray;last_update_check=cache.last_update_check;memory=cache.speakers;
        std::filesystem::create_directories(folder);
        controller=std::make_unique<Controller>([this]{PostMessageW(dispatcher,StateMessage,0,0);},std::move(cache.initial));
        load_widget_slots(read_library());   // the buttons of the speaker in use, so after the controller knows it
        // Results reach the page after the state they produced (both travel through the dispatcher).
        controller->on_result=[this](int id,bool ok,const std::string& error){post([this,id,ok,error]{web_result(id,ok,error);});};
        refresh_speakers();
        power_notification=RegisterSuspendResumeNotification(dispatcher,DEVICE_NOTIFY_WINDOW_HANDLE);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;filter.dbcc_classguid=GUID_BTHPORT_DEVICE_INTERFACE;
        interface_notification=RegisterDeviceNotificationW(dispatcher,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
        watch_radios();
        if(show)open(page);
        SetTimer(dispatcher,1,30000,nullptr);
    }
    void interest(){bool active=editor!=nullptr&&IsWindowVisible(editor);
        controller->release_always(editor!=nullptr&&!IsWindowVisible(editor));for(auto& [id,widget]:widgets)active|=widget.active;
        active|=Clock::now()<tray_until;   // the tray menu's buttons need the controls for a moment
        controller->interest(active);}
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
                auto card=make_card(s,record.size,record.view,icons,logging,widget_slots,widget_speakers);
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
        auto state=controller->snapshot();update_widgets();flush_waiting();refresh_editor();tray_play();
        if(state.checked&&!state.pending)save(state);
        if(!state.error.empty())diagnostic(state.error);
        maybe_exit();
    }
    void save(const Snapshot& s){
        try{JsonObject j;j.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(s.address)));
            j.SetNamedValue(L"logging",JsonValue::CreateBooleanValue(logging));
            j.SetNamedValue(L"release_when_hidden",JsonValue::CreateBooleanValue(s.release_when_hidden));
            j.SetNamedValue(L"chosen",JsonValue::CreateBooleanValue(chosen));
            j.SetNamedValue(L"check_updates",JsonValue::CreateBooleanValue(check_updates));
            j.SetNamedValue(L"tray",JsonValue::CreateBooleanValue(tray));
            j.SetNamedValue(L"last_update_check",JsonValue::CreateNumberValue(static_cast<double>(last_update_check)));
            write_speaker(j,s);
            // This speaker's own entry, only once it has reported something (a fresh choice keeps its old one).
            if(!memory)memory=JsonObject();
            if(!s.address.empty()){auto key=winrt::to_hstring(s.address);
                if(s.checked){JsonObject one;write_speaker(one,s);memory.SetNamedValue(key,one);}
                else{   // not read yet: keep what it last reported, but its own settings (the phone-app release) still count
                    JsonObject one=memory.HasKey(key)&&memory.GetNamedValue(key).ValueType()==JsonValueType::Object?memory.GetNamedObject(key):JsonObject();
                    one.SetNamedValue(L"release_when_hidden",JsonValue::CreateBooleanValue(s.release_when_hidden));memory.SetNamedValue(key,one);}}
            j.SetNamedValue(L"speakers",JsonObject::Parse(memory.Stringify()));
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
        if(verb==L"view.speakers")refresh_speakers();   // paired a moment ago: show it
        if(navigate_widget(view,verb)){update_widgets();return;}
        auto state=controller->snapshot();
        // Speakers page: switch to another paired speaker (the one in use, not linked, tries again).
        if(auto index=suffix_number(verb,L"speaker.choose.");index>=0){
            if(index<static_cast<int>(widget_speakers.size())){auto sp=widget_speakers[index];
                if(sp.in_use)controller->send({Operation::Retry});
                else choose(sp.address);
                view.page=WidgetPage::Home;update_widgets();}
            return;
        }
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
        if(!std::filesystem::exists(content/L"index.html"))throw std::runtime_error("The app's files are missing from the package.");
        // The page's last theme, so the window does not flash the other colour before it paints.
        std::string theme;{std::ifstream in(folder/L"theme",std::ios::binary);std::getline(in,theme);}
        web=std::make_unique<WebViewHost>(content,folder/L"WebView2",theme=="dark");
        web->on_message=[this](const std::wstring& text){web_message(text);};
        web->on_closed=[this]{spectrum.stop();editor=nullptr;waiting.clear();quitting=false;interest();post([this]{if(web&&!web->window)web.reset();update_tray();maybe_exit();});};
        // Close to tray: the window hides and, like a closed one, stops listening and lets the speaker go.
        web->on_close=[this]{return tray&&!quitting;};
        web->on_shown=[this](bool shown){if(shown)spectrum.start();else spectrum.stop();interest();update_tray();};
        // No WebView2 runtime (or it failed to start): use the native window instead.
        web->on_failed=[this](HRESULT hr){diagnostic("WebView2 unavailable: "+winrt::to_string(winrt::hresult_error(hr).message()));
            post([this]{web_failed=true;bool showing=web&&web->visible();web.reset();update_tray();if(showing)open_native(0);});};
        spectrum.on_frame=[this](const Spectrum::Levels& levels,bool playing,bool listening){
            JsonObject m;m.SetNamedValue(L"type",JsonValue::CreateStringValue(L"spectrum"));
            m.SetNamedValue(L"playing",JsonValue::CreateBooleanValue(playing));m.SetNamedValue(L"listening",JsonValue::CreateBooleanValue(listening));
            JsonArray bands;for(float v:levels)bands.Append(JsonValue::CreateNumberValue(std::round(v*100)/100));m.SetNamedValue(L"bands",bands);
            auto json=std::wstring(m.Stringify());post([this,json]{if(web&&web->window)web->post(json);});
        };
        editor=web->window;
        if(start_hidden){start_hidden=false;update_tray();}else web->show();   // shown: on_shown starts the spectrum
        interest();
        check_update(false);   // the daily look, if it is due and switched on
        query_startup({});
    }
    // ---- Tray and Start with Windows ----------------------------------------------------------
    // The icon shows while the window is open and Close to tray is on, and always while it is hidden
    // (otherwise nothing would bring it back).
    NOTIFYICONDATAW tray_data(){
        NOTIFYICONDATAW data{sizeof(data)};data.hWnd=tray_window;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
        data.uCallbackMessage=TrayMessage;data.uVersion=NOTIFYICON_VERSION_4;
        data.hIcon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
        wcscpy_s(data.szTip,L"Tribit Connect");return data;
    }
    void update_tray(){
        const bool want=tray_window&&web&&web->window&&(tray||!web->visible());
        if(want&&!in_tray){auto data=tray_data();in_tray=Shell_NotifyIconW(NIM_ADD,&data);if(in_tray)Shell_NotifyIconW(NIM_SETVERSION,&data);}
        else if(!want&&in_tray){auto data=tray_data();Shell_NotifyIconW(NIM_DELETE,&data);in_tray=false;}
    }
    // Opening from the tray: Windows sometimes gives the focus back to the taskbar just after the
    // click, so the window is brought forward again a moment later (timer 3).
    void open_from_tray(){open(0);SetTimer(dispatcher,3,250,nullptr);}
    static std::wstring menu_text(const std::string& text){std::wstring out;for(wchar_t c:std::wstring(winrt::to_hstring(text))){out+=c;if(c==L'&')out+=c;}return out;}
    // The menu: the speaker and its battery, the widget's six buttons (the one playing is ticked), Open, Quit.
    // The window is hidden, so the speaker's controls may have been let go: opening the menu opens them
    // again for a short while (tray_until), and a button picked before the link is up plays once it is.
    void tray_event(UINT event){
        if(event==NIN_SELECT||event==NIN_KEYSELECT||event==WM_LBUTTONUP){open_from_tray();return;}   // opening twice is harmless
        if(event!=WM_CONTEXTMENU&&event!=WM_RBUTTONUP)return;
        // A right-click can arrive as both messages; the second must not reopen the menu just closed.
        if(GetTickCount()-tray_menu_closed<300)return;
        auto s=controller->snapshot();const bool linked=s.ready&&s.fresh,known=!s.address.empty();
        if(known){tray_until=Clock::now()+25s;interest();SetTimer(dispatcher,4,26000,nullptr);}
        std::string name="XSound Plus 2";for(auto& sp:widget_speakers)if(sp.in_use)name=sp.name;
        std::wstring head=menu_text(name)+(!known?L"":linked?(s.battery>=0?L"  \u00B7  "+std::to_wstring(s.battery)+L"%":L""):
            s.battery>=0?L"  \u00B7  not connected, "+std::to_wstring(s.battery)+L"% when last seen":L"  \u00B7  not connected");
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,(known?head:std::wstring(L"No speaker chosen")).c_str());
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(size_t i=0;i<widget_slots.size();++i){auto& slot=widget_slots[i];
            AppendMenuW(menu,MF_STRING|(known?0:MF_GRAYED)|(linked&&same_eq(s.preset,s.bands,slot)?MF_CHECKED:0),10+i,menu_text(slot.name).c_str());}
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,1,L"Open Tribit Connect");AppendMenuW(menu,MF_STRING,2,L"Quit");
        POINT at{};GetCursorPos(&at);SetForegroundWindow(tray_window);
        const int picked=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,at.x,at.y,0,tray_window,nullptr);DestroyMenu(menu);tray_menu_closed=GetTickCount();
        PostMessageW(tray_window,WM_NULL,0,0);   // lets the menu close properly when nothing was picked
        if(picked==1)open_from_tray();
        else if(picked==2&&web&&web->window){quitting=true;PostMessageW(web->window,WM_CLOSE,0,0);}
        else if(picked>=10&&picked<10+static_cast<int>(widget_slots.size())){tray_wanted=picked-10;tray_wanted_until=Clock::now()+15s;tray_play();}
    }
    // Plays the tray button that was picked, as soon as the speaker can take it.
    void tray_play(){
        if(tray_wanted<0)return;
        if(Clock::now()>tray_wanted_until){tray_wanted=-1;return;}
        auto s=controller->snapshot();if(!s.ready||!s.fresh||s.pending)return;
        const int index=tray_wanted;tray_wanted=-1;
        if(auto command=setting_command(s,L"eq.slot."+std::to_wstring(index),widget_slots))controller->send(*command);
    }
    // Start with Windows is the package's startup task, which Windows owns: read it, or ask to change it,
    // off the UI thread, then show what Windows says (it can refuse, e.g. when turned off in Settings > Apps > Startup).
    void query_startup(std::optional<bool> want){
        std::thread([this,want,alive=alive]{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            using winrt::Windows::ApplicationModel::StartupTask;using winrt::Windows::ApplicationModel::StartupTaskState;
            std::wstring state=L"unavailable";   // not packaged (a development build)
            try{auto task=StartupTask::GetAsync(StartupTaskId).get();auto s=task.State();
                if(want){if(*want)s=task.RequestEnableAsync().get();else{task.Disable();s=task.State();}}
                state=s==StartupTaskState::Enabled?L"on":s==StartupTaskState::EnabledByPolicy?L"policy-on":s==StartupTaskState::DisabledByPolicy?L"policy-off":
                    s==StartupTaskState::DisabledByUser?L"user-off":L"off";}catch(...){}
            if(*alive)post([this,state]{startup=state;refresh_editor();});
        }).detach();
    }
    void refresh_editor(){if(native)native->refresh();if(web&&web->window)web->post(state_json());}
    std::filesystem::path library_path()const{return folder/L"profiles.json";}
    static bool read_bands(const JsonObject& o,Bands& out){
        if(!o.HasKey(L"bands"))return false;auto a=o.GetNamedArray(L"bands");if(a.Size()!=out.size())return false;
        for(uint32_t i=0;i<a.Size();++i)out[i]=static_cast<int>(a.GetNumberAt(i));
        return true;
    }
    // Everything the page shows, in the same shape the browser mock sends.
    static bool developer_mode(){
        DWORD value=0,size=sizeof(value);
        return RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock",L"AllowDevelopmentWithoutDevLicense",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS&&value==1;
    }
    // Is the widget on the board? Asked of Windows at most every few seconds (state goes out often).
    bool widget_added(){
        if(!widgets.empty())return true;
        if(Clock::now()<widget_asked+5s)return widget_known;
        widget_asked=Clock::now();
        try{widget_known=wp::WidgetManager::GetDefault().GetWidgetInfos().size()>0;}catch(...){widget_known=false;}
        return widget_known;
    }
    Clock::time_point widget_asked{};bool widget_known{};
    std::wstring state_json(bool with_library=false){
        auto s=controller->snapshot();const bool connected=s.ready&&s.fresh;
        std::wstring status=s.pending&&!connected?L"connecting":connected&&s.pending?L"updating":connected?L"connected":
            (s.status=="Bluetooth unavailable"||!s.error.empty())?L"unavailable":L"disconnected";
        JsonObject speaker;
        speaker.SetNamedValue(L"connected",JsonValue::CreateBooleanValue(connected));
        speaker.SetNamedValue(L"status",JsonValue::CreateStringValue(status));
        speaker.SetNamedValue(L"settled",JsonValue::CreateBooleanValue(s.settled));
        speaker.SetNamedValue(L"reason",JsonValue::CreateStringValue(connected?L"":winrt::to_hstring(s.reason)));
        speaker.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(s.address)));
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
        settings.SetNamedValue(L"chosen",JsonValue::CreateBooleanValue(chosen));
        settings.SetNamedValue(L"checkUpdates",JsonValue::CreateBooleanValue(check_updates));
        settings.SetNamedValue(L"tray",JsonValue::CreateBooleanValue(tray));
        // For the widget's two steps in Settings: Windows shows a widget from outside the Store only in Developer Mode.
        settings.SetNamedValue(L"devMode",JsonValue::CreateBooleanValue(developer_mode()));
        settings.SetNamedValue(L"widgetAdded",JsonValue::CreateBooleanValue(widget_added()));
        settings.SetNamedValue(L"startup",JsonValue::CreateStringValue(startup));
        settings.SetNamedValue(L"version",JsonValue::CreateStringValue(app_version()));
        JsonObject update;update.SetNamedValue(L"state",JsonValue::CreateStringValue(update_state));update.SetNamedValue(L"version",JsonValue::CreateStringValue(update_version));
        settings.SetNamedValue(L"update",update);
        JsonObject root;
        root.SetNamedValue(L"type",JsonValue::CreateStringValue(L"state"));
        root.SetNamedValue(L"speaker",speaker);root.SetNamedValue(L"settings",settings);
        // The library goes to the page once, in the reply to `ready`; it is the page's from then on.
        if(with_library){
            root.SetNamedValue(L"library",read_library());
            if(library_error)root.SetNamedValue(L"libraryError",JsonValue::CreateBooleanValue(true));
        }
        return std::wstring(root.Stringify());
    }
    // null when there is no library yet. A file that is there but cannot be used (unreadable, cut short,
    // not a JSON object, or absurdly large) is a different thing: it is copied aside as
    // profiles.bad-<time>.json and library_error is set, so the page does not start a new library
    // over the user's profiles.
    JsonValue read_library(){
        std::error_code ec;auto path=library_path();
        if(!std::filesystem::exists(path,ec))return JsonValue::CreateNullValue();
        try{
            if(std::filesystem::file_size(path)<(8u<<20)){
                std::ifstream in(path,std::ios::binary);std::string text{std::istreambuf_iterator<char>(in),{}};
                JsonValue parsed{nullptr};
                if(in&&JsonValue::TryParse(winrt::to_hstring(text),parsed)&&parsed.ValueType()==JsonValueType::Object){library_error=false;return parsed;}
            }
        }catch(...){}
        if(!library_error){library_error=true;diagnostic("profiles.json could not be read; kept a copy as profiles.bad");
            std::filesystem::copy_file(path,folder/(L"profiles.bad-"+std::to_wstring(tribit::updates::now_seconds())+L".json"),std::filesystem::copy_options::skip_existing,ec);}
        return JsonValue::CreateNullValue();
    }
    // Sound Lab saves the widget's buttons with the library, already resolved to names and curves.
    void load_widget_slots(const JsonValue& library){
        widget_slots=default_slots();
        if(library.ValueType()!=JsonValueType::Object)return;
        // Each speaker has its own six buttons (widgetSlotsBy, by address); widgetSlots is the fallback.
        auto root=library.GetObject();auto slots=root.GetNamedArray(L"widgetSlots",JsonArray());
        if(controller&&root.HasKey(L"widgetSlotsBy")&&root.GetNamedValue(L"widgetSlotsBy").ValueType()==JsonValueType::Object){
            auto by=root.GetNamedObject(L"widgetSlotsBy");auto key=winrt::to_hstring(controller->snapshot().address);
            if(by.HasKey(key)&&by.GetNamedValue(key).ValueType()==JsonValueType::Array)slots=by.GetNamedArray(key);
        }
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
        // The timer ends a wait even if no state change arrives to flush it.
        if(s.ready&&s.fresh&&s.pending&&waiting.size()<8){waiting.push_back({std::move(c),Clock::now()+4s});SetTimer(dispatcher,2,1000,nullptr);return;}
        web_result(c.id,false,s.ready&&s.fresh?"The speaker is busy. Try again.":"The speaker is not connected.");
    }
    void flush_waiting(){
        while(!waiting.empty()){
            auto& [c,deadline]=waiting.front();
            if(Clock::now()>deadline){web_result(c.id,false,"The speaker did not respond in time.");waiting.pop_front();continue;}
            if(!controller->send(c))break;
            waiting.pop_front();
        }
        if(waiting.empty())KillTimer(dispatcher,2);
    }
    void save_library(const std::wstring& json){
        try{
            // profiles.bak: the library as it was when this session first saved, if it was readable.
            if(!library_backed_up){library_backed_up=true;std::error_code ec;
                if(!library_error&&std::filesystem::exists(library_path(),ec))std::filesystem::copy_file(library_path(),folder/L"profiles.bak",std::filesystem::copy_options::overwrite_existing,ec);}
            auto temporary=folder/L"profiles.tmp";
            {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<winrt::to_string(json);out.flush();if(!out)throw std::runtime_error("Could not save profiles.");}
            if(!MoveFileExW(temporary.c_str(),library_path().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();
        }catch(const std::exception& e){diagnostic(e.what());}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    // Every request (a message with an id) gets a result, including one this code could not make sense
    // of: without it the page would wait for an answer that never comes.
    void web_message(const std::wstring& text){
        JsonObject m{nullptr};if(!JsonObject::TryParse(text,m))return;
        int id=0;try{id=static_cast<int>(m.GetNamedNumber(L"id",0));}catch(...){}
        try{handle_message(m,id);}
        catch(const winrt::hresult_error& e){diagnostic("page message: "+winrt::to_string(e.message()));web_result(id,false,"The request was not understood.");}
        catch(const std::exception& e){diagnostic(std::string("page message: ")+e.what());web_result(id,false,"The request was not understood.");}
    }
    static bool bands_in_range(const Bands& bands){return std::all_of(bands.begin(),bands.end(),[](int b){return b>=-8&&b<=8;});}
    void handle_message(const JsonObject& m,int id){
        const std::wstring type(m.GetNamedString(L"type",L""));
        if(type==L"ready"){web->page_ready();web->post(state_json(true));return;}
        if(type==L"painted"){web->reveal();return;}   // the page has drawn: uncloak the window (see WebViewHost::show)
        // A script error on the page, for the diagnostic log (written only while it is switched on).
        if(type==L"log"){diagnostic("page: "+tribit::narrow(std::wstring(m.GetNamedString(L"text",L"")).substr(0,1500)));return;}
        if(type==L"window"){web->command(std::wstring(m.GetNamedString(L"action",L"")));return;}
        if(type==L"apply"){
            Command c{Operation::Preset};c.id=id;const int preset=static_cast<int>(m.GetNamedNumber(L"preset",-1));
            if(preset==tribit::Custom){c.operation=Operation::CustomEq;if(!read_bands(m,c.bands)||!bands_in_range(c.bands)){web_result(id,false,"That curve cannot be played.");return;}}
            else if(!valid_preset(preset)){web_result(id,false,"Unknown program.");return;}
            else c.value=preset;
            submit(std::move(c));return;
        }
        if(type==L"assign"){
            Command c{Operation::Buttons};c.id=id;auto slots=m.GetNamedArray(L"slots",JsonArray());
            if(slots.Size()!=3){web_result(id,false,"Three lights are needed.");return;}
            for(uint32_t i=0;i<3;++i){
                auto slot=slots.GetObjectAt(i);c.buttons[i]=static_cast<int>(slot.GetNamedNumber(L"preset",-1));
                if(c.buttons[i]==tribit::Custom?(!read_bands(slot,c.button_bands[i])||!bands_in_range(c.button_bands[i])):!valid_preset(c.buttons[i])){web_result(id,false,"A light holds something that cannot be written back. Try again in a moment.");return;}
            }
            submit(std::move(c));return;
        }
        if(type==L"setPrompt"){Command c{Operation::Prompt};c.id=id;c.index=static_cast<int>(m.GetNamedNumber(L"index",-1));if(c.index<0||c.index>4){web_result(id,false,"Unknown beep.");return;}c.value=m.GetNamedBoolean(L"value",false)?1:0;submit(std::move(c));return;}
        if(type==L"setAutoShutdown"){Command c{Operation::AutoShutdown};c.id=id;c.value=m.GetNamedBoolean(L"value",false)?1:0;submit(std::move(c));return;}
        if(type==L"setRelease"){release(m.GetNamedBoolean(L"value",false));update_widgets();refresh_editor();return;}
        if(type==L"setLogging"){logging=m.GetNamedBoolean(L"value",false);save(controller->snapshot());update_widgets();refresh_editor();return;}
        if(type==L"saveLibrary"){if(m.HasKey(L"library")){auto library=m.GetNamedValue(L"library");save_library(std::wstring(library.Stringify()));load_widget_slots(library);update_widgets();}return;}
        // Settings > Remove my data: profiles, settings and the log go; the page has already reset itself.
        if(type==L"removeData"){std::error_code ec;
            for(auto name:{L"profiles.json",L"profiles.tmp",L"profiles.bak",L"state.json",L"state.tmp",L"theme",L"diagnostics.log"})std::filesystem::remove(folder/name,ec);
            std::vector<std::filesystem::path> aside;
            for(auto& entry:std::filesystem::directory_iterator(folder,ec))if(entry.path().filename().wstring().starts_with(L"profiles.bad-"))aside.push_back(entry.path());
            for(auto& path:aside)std::filesystem::remove(path,ec);
            library_error=false;library_backed_up=true;
            logging=false;chosen=false;check_updates=true;last_update_check=0;tray=false;memory=JsonObject();update_tray();
            load_widget_slots(JsonValue::CreateNullValue());update_widgets();refresh_editor();return;}
        if(type==L"theme"){const bool dark=m.GetNamedBoolean(L"dark",false);web->theme(dark);
            try{std::ofstream(folder/L"theme",std::ios::binary|std::ios::trunc)<<(dark?"dark":"light");}catch(...){}return;}
        if(type==L"retry"){controller->send({Operation::Retry});return;}
        if(type==L"listSpeakers"){web->post(speakers_json());return;}
        // Only a paired, supported speaker can be chosen.
        if(type==L"chooseSpeaker"){
            auto wanted=tribit::parse_address(winrt::to_string(m.GetNamedString(L"address",L"")));if(!wanted)return;
            auto list=tribit::list_speakers();
            if(std::none_of(list.begin(),list.end(),[&](auto& d){return d.address==*wanted&&d.supported;}))return;
            choose(tribit::format_address(*wanted));return;
        }
        // The speaker already in use was picked: only remember that the question was answered.
        if(type==L"markChosen"){chosen=true;save(controller->snapshot());web->post(speakers_json());return;}
        if(type==L"setCheckUpdates"){check_updates=m.GetNamedBoolean(L"value",true);save(controller->snapshot());refresh_editor();return;}
        if(type==L"checkUpdate"){check_update(true);return;}
        if(type==L"setTray"){tray=m.GetNamedBoolean(L"value",false);save(controller->snapshot());update_tray();refresh_editor();return;}
        if(type==L"setStartup"){query_startup(m.GetNamedBoolean(L"value",false));return;}
        if(type==L"installUpdate"){install_update();return;}
        if(type==L"openDeveloper"){ShellExecuteW(nullptr,L"open",L"ms-settings:developers",nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(type==L"openBluetooth"){ShellExecuteW(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;}
        // The add-your-speaker site (repo tribit-connect-submit). Only this fixed address is opened.
        // An optional model name rides along as ?model=, percent-encoded; nothing else is added.
        if(type==L"openSubmit"){
            std::wstring url=L"https://tribit-connect-submit.vercel.app/";
            auto model=tribit::narrow(std::wstring(m.GetNamedString(L"model",L"")));
            if(!model.empty()&&model.size()<=48){
                std::wstring query=L"?model=";
                for(unsigned char c:model){
                    if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')query+=static_cast<wchar_t>(c);
                    else{wchar_t hex[4];swprintf(hex,4,L"%%%02X",c);query+=hex;}
                }
                url+=query;
            }
            ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;
        }
    }
    // ---- Updates (GitHub Releases; see updates.hpp) ----------------------------------------------
    void set_update(std::wstring state,std::wstring version={}){update_state=std::move(state);update_version=std::move(version);refresh_editor();}
    // force: Check now, which skips the daily limit and the switch.
    void check_update(bool force){
        using namespace tribit::updates;
        if(update_state==L"checking"||update_state==L"downloading"||update_state==L"installing")return;
        auto current=parse_version(tribit::updates::app_version());
        if(!current){if(force)set_update(L"dev");return;}   // an unpackaged build has no version to compare (stays: it never changes)
        if(!force&&(!check_updates||now_seconds()-last_update_check<24*3600))return;
        last_update_check=now_seconds();save(controller->snapshot());
        set_update(L"checking");const int seq=++update_seq;
        std::thread([this,cur=*current,seq,alive=alive]{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto started=Clock::now();
            std::optional<Release> release;bool failed=false;
            try{release=newer_release(within(latest_json(),20s),cur);}catch(...){failed=true;}   // offline, rate-limited or too slow
            // Long enough to read "Checking…", so a second Check now visibly happens.
            std::this_thread::sleep_until(started+700ms);
            if(!*alive)return;
            post([this,release,failed]{available=release;set_update(failed?L"failed":release?L"available":L"current",release?release->version:L"");});
            // Then the key goes back to "Check now", ready for another look (an offer stays until used).
            if(release)return;
            std::this_thread::sleep_for(4s);
            if(!*alive)return;
            post([this,seq]{if(seq==update_seq&&(update_state==L"current"||update_state==L"failed"))set_update(L"idle");});
        }).detach();
    }
    // A request's result, or an exception once `limit` has passed (the request is cancelled).
    template<class Async> static auto within(Async operation,std::chrono::seconds limit){
        if(operation.wait_for(limit)!=winrt::Windows::Foundation::AsyncStatus::Completed){operation.Cancel();throw std::runtime_error("The request took too long.");}
        return operation.GetResults();
    }
    int update_seq{};   // the latest check, so an older one's reset never clears a newer result
    void install_update(){
        if(!available||update_state==L"downloading"||update_state==L"installing")return;
        set_update(L"downloading",available->version);
        std::thread([this,release=*available,alive=alive]{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            winrt::hstring setup;try{setup=within(tribit::updates::download(release),120s);}catch(...){}
            // From here until it runs, nothing else may write the file: it is held open without write
            // sharing, and its signature is checked again under that hold.
            HANDLE hold=INVALID_HANDLE_VALUE;
            if(!setup.empty()){hold=CreateFileW(setup.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(hold==INVALID_HANDLE_VALUE||!tribit::updates::signed_by_us(std::filesystem::path(std::wstring(setup)))){if(hold!=INVALID_HANDLE_VALUE)CloseHandle(hold);hold=INVALID_HANDLE_VALUE;setup=L"";}}
            if(!*alive){if(hold!=INVALID_HANDLE_VALUE)CloseHandle(hold);return;}
            post([this,setup,hold,version=release.version]{
                if(setup.empty()){set_update(L"unverified",version);return;}
                set_update(L"installing",version);
                // Setup closes this app and the widget for the moment it takes, then reopens the app.
                const auto started=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",setup.c_str(),L"/update",nullptr,SW_SHOWNORMAL));
                CloseHandle(hold);
                if(started<=32)set_update(L"stalled",version);
            });
            // Setup ends this process within moments. Still here after a minute: it did not go through.
            std::this_thread::sleep_for(60s);
            if(*alive)post([this,version=release.version]{if(update_state==L"installing")set_update(L"stalled",version);});
        }).detach();
    }
    // Switch to another paired speaker (Speakers page or widget), showing what it last reported.
    void choose(const std::string& address){
        save(controller->snapshot());    // keeps the speaker being left in memory
        Snapshot known;auto key=winrt::to_hstring(address);
        known.release_when_hidden=controller->snapshot().release_when_hidden;   // a speaker never set keeps the current choice
        if(memory&&memory.HasKey(key)&&memory.GetNamedValue(key).ValueType()==JsonValueType::Object)read_speaker(memory.GetNamedObject(key),known);
        chosen=true;controller->choose(address,known);
        // That speaker's own widget buttons and "Let the phone app connect".
        controller->release_when_hidden(known.release_when_hidden);load_widget_slots(read_library());
        save(controller->snapshot());
        refresh_speakers();update_widgets();
        if(web&&web->window)web->post(speakers_json());
    }
    // Paired Tribit speakers for the Speakers page, and which one this app remembers.
    std::wstring speakers_json(){
        JsonArray list;
        try{
            for(auto& d:tribit::list_speakers()){
                JsonObject o;
                o.SetNamedValue(L"name",JsonValue::CreateStringValue(d.name));
                o.SetNamedValue(L"model",JsonValue::CreateStringValue(d.model));
                o.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(tribit::format_address(d.address))));
                o.SetNamedValue(L"connected",JsonValue::CreateBooleanValue(d.connected));
                o.SetNamedValue(L"supported",JsonValue::CreateBooleanValue(d.supported));
                // When this app last read it (0 = never); the page says "last seen" for an off speaker.
                auto key=winrt::to_hstring(tribit::format_address(d.address));
                double seen=memory&&memory.HasKey(key)&&memory.GetNamedValue(key).ValueType()==JsonValueType::Object?memory.GetNamedObject(key).GetNamedNumber(L"checked",0):0;
                o.SetNamedValue(L"seen",JsonValue::CreateNumberValue(seen*1000));
                list.Append(o);
            }
        }catch(...){}
        JsonObject root;
        root.SetNamedValue(L"type",JsonValue::CreateStringValue(L"speakers"));
        root.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(controller->snapshot().address)));
        root.SetNamedValue(L"chosen",JsonValue::CreateBooleanValue(chosen));
        root.SetNamedValue(L"list",list);
        return std::wstring(root.Stringify());
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
        // A speaker linked, dropped or was paired: the widget's Speakers page and Switch follow.
        if(event==DBT_CUSTOMEVENT||event==DBT_DEVICEARRIVAL||event==DBT_DEVICEREMOVECOMPLETE){refresh_speakers();update_widgets();}
    }
    // Paired speakers this app can control, for the widget (twins go by their Windows names).
    void refresh_speakers(){
        WidgetSpeakers next;
        try{
            auto list=tribit::list_speakers();auto here=controller->snapshot().address;
            for(auto& d:list){if(!d.supported)continue;
                bool twin=std::count_if(list.begin(),list.end(),[&](auto& o){return o.supported&&o.model==d.model;})>1;
                auto address=tribit::format_address(d.address);
                next.push_back({tribit::narrow(twin?d.name:d.model),address,address==here,d.connected});}
            // The one in use first, as on the Speakers page.
            std::stable_partition(next.begin(),next.end(),[](auto& sp){return sp.in_use;});
        }catch(...){}
        widget_speakers=std::move(next);
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
            if(message==TrayMessage){self->tray_event(LOWORD(lparam));return 0;}
            // Explorer restarting clears every tray icon; put ours back.
            static const UINT taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
            if(message==taskbar_created&&window==self->tray_window){self->in_tray=false;self->update_tray();return 0;}
            if(message==WM_TIMER&&window==self->dispatcher&&wparam==2){self->flush_waiting();return 0;}
            if(message==WM_TIMER&&window==self->dispatcher&&wparam==3){KillTimer(window,3);if(self->web&&self->web->visible())self->web->front();return 0;}
            if(message==WM_TIMER&&window==self->dispatcher&&wparam==4){KillTimer(window,4);self->interest();return 0;}
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
    bool startup=false;
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        // Start with Windows: the startup task launches this process with no arguments, so ask how it was activated.
        if(!provider)try{if(auto activated=winrt::Windows::ApplicationModel::AppInstance::GetActivatedEventArgs())startup=activated.Kind()==winrt::Windows::ApplicationModel::Activation::ActivationKind::StartupTask;}catch(...){}
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
            App app;app.pipe_name=pipe;app.start_hidden=startup;app.init(!provider,static_cast<int>(start_page));app.start_pipe(descriptor);
            auto factory=winrt::make<ProviderFactory>(app);DWORD cookie{};
            winrt::check_hresult(CoRegisterClassObject(ProviderClsid,factory.get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie));
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(app.dialog&&IsDialogMessageW(app.dialog,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
            CoRevokeClassObject(cookie);
        }
        LocalFree(descriptor);CloseHandle(mutex);return 0;
    }catch(const winrt::hresult_error& e){if(!provider)MessageBoxW(nullptr,e.message().c_str(),L"Tribit Connect",MB_ICONERROR);return 1;}
    catch(const std::exception& e){if(!provider)MessageBoxW(nullptr,winrt::to_hstring(e.what()).c_str(),L"Tribit Connect",MB_ICONERROR);return 1;}
}
