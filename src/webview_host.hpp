#pragma once
// The companion window: a frameless-looking top-level window hosting the Sound Lab frontend in
// WebView2. The page draws its own title bar; WebView2's non-client region support turns its
// `app-region: drag` area into the window caption (drag, snap, double-click to maximize), and
// the window keeps Windows' own resize borders on the sides and bottom.
#include <windows.h>
#include <WebView2.h>
#include <winrt/base.h>
#include <dwmapi.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// A COM callback object around a lambda, for WebView2's completion and event handlers.
template<class Interface,class... Args>
struct WebViewHandler:winrt::implements<WebViewHandler<Interface,Args...>,Interface>{
    std::function<HRESULT(Args...)> fn;
    explicit WebViewHandler(std::function<HRESULT(Args...)> f):fn(std::move(f)){}
    HRESULT __stdcall Invoke(Args... args) noexcept override{try{return fn(args...);}catch(...){return E_FAIL;}}
};

class WebViewHost {
public:
    HWND window{};
    std::function<void(const std::wstring&)> on_message; // JSON text from the page
    std::function<void()> on_closed;
    std::function<void(HRESULT)> on_failed;              // WebView2 could not start (runtime missing)

    WebViewHost(std::filesystem::path content,std::filesystem::path user_data,bool dark=false):content_(std::move(content)),user_data_(std::move(user_data)),dark_(dark){
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TribitConnect.SoundLab";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=nullptr;
        wc.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));RegisterClassW(&wc);
        const UINT dpi=GetDpiForSystem();
        window=CreateWindowExW(0,wc.lpszClassName,L"Tribit Connect",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
            MulDiv(1280,dpi,96),MulDiv(800,dpi,96),nullptr,nullptr,wc.hInstance,this);
        if(!window)winrt::throw_last_error();
        // Keep DWM's shadow and rounded corners once the caption is gone.
        MARGINS margins{0,0,1,0};DwmExtendFrameIntoClientArea(window,&margins);
        SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
        theme(dark);
        start();
    }
    ~WebViewHost(){if(window)DestroyWindow(window);}
    void show(){if(IsIconic(window))ShowWindow(window,SW_RESTORE);else ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
    // Messages wait until the page has loaded its bridge.
    void post(const std::wstring& json){if(view_&&ready_)view_->PostWebMessageAsJson(json.c_str());else if(pending_.size()<64)pending_.push_back(json);}
    void page_ready(){ready_=true;for(auto& m:pending_)view_->PostWebMessageAsJson(m.c_str());pending_.clear();}
    // The page's light or dark panel colour, behind the page and in the window frame.
    void theme(bool dark){
        dark_=dark;BOOL value=dark;DwmSetWindowAttribute(window,20/*DWMWA_USE_IMMERSIVE_DARK_MODE*/,&value,sizeof(value));
        if(controller_)if(auto c2=controller_.try_as<ICoreWebView2Controller2>())c2->put_DefaultBackgroundColor(background());
        InvalidateRect(window,nullptr,TRUE);
    }
    void command(const std::wstring& action){
        if(action==L"minimize")ShowWindow(window,SW_MINIMIZE);
        else if(action==L"maximize")ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);
        else if(action==L"close")PostMessageW(window,WM_CLOSE,0,0);
    }
private:
    std::filesystem::path content_,user_data_;
    winrt::com_ptr<ICoreWebView2Controller> controller_;
    winrt::com_ptr<ICoreWebView2> view_;
    std::vector<std::wstring> pending_;
    bool ready_{},dark_{};
    COREWEBVIEW2_COLOR background()const{return dark_?COREWEBVIEW2_COLOR{255,0x22,0x22,0x1f}:COREWEBVIEW2_COLOR{255,0xec,0xe8,0xde};}

    void start(){
        using EnvDone=WebViewHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>;
        using CtlDone=WebViewHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>;
        HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,user_data_.c_str(),nullptr,winrt::make<EnvDone>([this](HRESULT result,ICoreWebView2Environment* env)->HRESULT{
            if(FAILED(result)||!env){fail(result);return S_OK;}
            return env->CreateCoreWebView2Controller(window,winrt::make<CtlDone>([this](HRESULT created,ICoreWebView2Controller* controller)->HRESULT{
                if(FAILED(created)||!controller){fail(created);return S_OK;}
                controller_.copy_from(controller);controller_->get_CoreWebView2(view_.put());
                configure();return S_OK;}).get());
        }).get());
        if(FAILED(hr))fail(hr);
    }
    void fail(HRESULT hr){if(on_failed)on_failed(hr);}
    void configure(){
        // The panel colour, so nothing flashes white before the page paints.
        if(auto c2=controller_.try_as<ICoreWebView2Controller2>())c2->put_DefaultBackgroundColor(background());
        winrt::com_ptr<ICoreWebView2Settings> settings;view_->get_Settings(settings.put());
        settings->put_IsStatusBarEnabled(FALSE);settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
#ifdef NDEBUG
        settings->put_AreDevToolsEnabled(FALSE);
#endif
        if(auto s3=settings.try_as<ICoreWebView2Settings3>())s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        if(auto s9=settings.try_as<ICoreWebView2Settings9>())s9->put_IsNonClientRegionSupportEnabled(TRUE);
        // The frontend is served from the package's app folder under a private host name.
        if(auto v3=view_.try_as<ICoreWebView2_3>())v3->SetVirtualHostNameToFolderMapping(L"app.tribit",content_.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        using Received=WebViewHandler<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>;
        EventRegistrationToken token{};
        view_->add_WebMessageReceived(winrt::make<Received>([this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
            // Only our own page may talk to the host.
            LPWSTR source{};args->get_Source(&source);std::wstring origin=source?source:L"";CoTaskMemFree(source);
            if(origin.rfind(L"https://app.tribit/",0)!=0)return S_OK;
            LPWSTR json{};if(SUCCEEDED(args->get_WebMessageAsJson(&json))&&json){std::wstring text(json);CoTaskMemFree(json);if(on_message)on_message(text);}
            return S_OK;}).get(),&token);
        // Links never navigate the window away from the app.
        using Starting=WebViewHandler<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>;
        view_->add_NavigationStarting(winrt::make<Starting>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
            LPWSTR uri{};args->get_Uri(&uri);std::wstring u=uri?uri:L"";CoTaskMemFree(uri);
            if(u.rfind(L"https://app.tribit/",0)!=0)args->put_Cancel(TRUE);return S_OK;}).get(),&token);
        resize();
        view_->Navigate(L"https://app.tribit/index.html");
    }
    void resize(){if(!controller_)return;RECT r{};GetClientRect(window,&r);controller_->put_Bounds(r);}
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WebViewHost*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WebViewHost*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
        switch(msg){
        case WM_NCCALCSIZE:
            if(wp){
                // Drop the caption but keep the standard side and bottom resize borders.
                auto* params=reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);const LONG top=params->rgrc[0].top;
                auto result=DefWindowProcW(hwnd,msg,wp,lp);
                params->rgrc[0].top=top;
                // A maximized window extends past the screen by its frame; keep the content on screen.
                if(IsZoomed(hwnd)){UINT dpi=GetDpiForWindow(hwnd);params->rgrc[0].top+=GetSystemMetricsForDpi(SM_CYFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);}
                return result;
            }
            break;
        case WM_GETMINMAXINFO:{
            UINT dpi=GetDpiForWindow(hwnd);auto* info=reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize={MulDiv(960,dpi,96),MulDiv(620,dpi,96)};return 0;}
        case WM_DPICHANGED:{
            auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        case WM_ERASEBKGND:{RECT r{};GetClientRect(hwnd,&r);auto c=self->background();auto brush=CreateSolidBrush(RGB(c.R,c.G,c.B));
            FillRect(reinterpret_cast<HDC>(wp),&r,brush);DeleteObject(brush);return 1;}
        case WM_SIZE:self->resize();return 0;
        case WM_MOVE:case WM_MOVING:if(self->controller_)self->controller_->NotifyParentWindowPositionChanged();break;
        case WM_ACTIVATE:if(self->controller_&&LOWORD(wp)!=WA_INACTIVE)self->controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);break;
        case WM_CLOSE:DestroyWindow(hwnd);return 0;
        case WM_DESTROY:
            if(self->controller_){self->controller_->Close();self->controller_=nullptr;self->view_=nullptr;}
            self->window=nullptr;
            if(self->on_closed)self->on_closed();
            return 0;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
};
