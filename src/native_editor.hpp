#pragma once
#include "widget_card.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

class NativeEditor {
public:
    HWND window{};
    std::function<Snapshot()> read;
    std::function<bool(Command)> send;
    std::function<void()> closed;
    std::function<bool()> get_logging;
    std::function<void(bool)> set_logging;
    std::function<void(bool)> set_release;
    NativeEditor(std::function<Snapshot()> reader,std::function<bool(Command)> sender,std::function<void()> on_close,
                 std::function<bool()> logging_reader,std::function<void(bool)> logging_writer,std::function<void(bool)> release_writer)
        :read(std::move(reader)),send(std::move(sender)),closed(std::move(on_close)),get_logging(std::move(logging_reader)),set_logging(std::move(logging_writer)),set_release(std::move(release_writer)) {
        background_=CreateSolidBrush(RGB(248,249,251));
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TribitConnect.NativeEditor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Tribit Connect",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,600,760,nullptr,nullptr,wc.hInstance,this);
        if(!window)winrt::throw_last_error();
        wc.lpszClassName=L"TribitConnect.Page";RegisterClassW(&wc);
        viewport_=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,0,0,window,nullptr,wc.hInstance,this);
        theme();
        add(L"STATIC",L"XSound Plus 2",10,24,18,-165,30);
        add(L"STATIC",L"Connecting...",11,24,54,-48,24);
        add(L"STATIC",L"",12,24,82,-48,24);
        add(L"BUTTON",L"Retry",103,-126,18,102,34,WS_TABSTOP|BS_OWNERDRAW);
        add(L"BUTTON",L"Sound",200,24,120,138,36,WS_TABSTOP|BS_OWNERDRAW);
        add(L"BUTTON",L"Device",201,172,120,138,36,WS_TABSTOP|BS_OWNERDRAW);
        add(L"BUTTON",L"Settings",202,320,120,138,36,WS_TABSTOP|BS_OWNERDRAW);

        add(L"STATIC",L"Equalizer preset",302,24,8,-48,24,0,0);
        add(L"COMBOBOX",L"Equalizer preset",410,24,38,-48,240,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0);
        for(auto id:kPresetOrder)SendMessageW(control(410),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(winrt::to_hstring(preset_label(id)).c_str()));
        add(L"STATIC",L"",412,24,82,-48,52,0,0);

        add(L"STATIC",L"",600,24,12,-48,170,0,1);
        add(L"BUTTON",L"Windows Bluetooth settings",104,24,196,254,36,WS_TABSTOP|BS_OWNERDRAW,1);

        add(L"STATIC",L"Voice prompts",310,24,8,-48,24,0,2);
        for(int i=0;i<5;++i)add(L"BUTTON",winrt::to_hstring(kPromptLabels[i]).c_str(),320+i,24,38+i*34,-48,30,WS_TABSTOP|BS_AUTOCHECKBOX,2);
        add(L"BUTTON",L"Auto shutdown when idle",330,24,218,-48,30,WS_TABSTOP|BS_AUTOCHECKBOX,2);
        add(L"STATIC",L"EQ button",311,24,268,-48,24,0,2);
        add(L"STATIC",L"Each press of the speaker's EQ button moves to the next light.",312,24,296,-48,42,0,2);
        for(int i=0;i<3;++i){
            add(L"STATIC",winrt::to_hstring(kLightLabels[i]).c_str(),340+i,24,350+i*42,120,30,0,2);
            add(L"COMBOBOX",winrt::to_hstring(kLightLabels[i]).c_str(),350+i,150,346+i*42,-48,240,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,2);
            for(auto id:kPresetOrder)SendMessageW(control(350+i),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(winrt::to_hstring(preset_label(id)).c_str()));
        }
        add(L"BUTTON",L"Apply",360,24,476,116,34,WS_TABSTOP|BS_OWNERDRAW,2);
        add(L"STATIC",L"Connection",313,24,534,-48,24,0,2);
        add(L"BUTTON",L"Release for phone app (15 s after the widget hides)",314,24,562,-48,30,WS_TABSTOP|BS_AUTOCHECKBOX,2);
        add(L"BUTTON",L"Enable diagnostic logging (maximum 5 MiB)",315,24,596,-48,30,WS_TABSTOP|BS_AUTOCHECKBOX,2);
        add(L"STATIC",L"The Tribit app on your phone cannot connect while these controls hold the speaker. Close this window and hide the widget, or turn on release above.\r\n\r\nAdd XSound Plus 2 from Win + W > Add widgets.",316,24,638,-48,110,0,2);
        theme();
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        SetWindowPos(window,nullptr,0,0,std::min<int>(static_cast<int>(600*scale_),work.right-work.left-48),std::min<int>(static_cast<int>(760*scale_),work.bottom-work.top-48),SWP_NOMOVE|SWP_NOZORDER);
        layout();refresh();
    }
    ~NativeEditor(){if(window)DestroyWindow(window);if(font_)DeleteObject(font_);if(title_font_)DeleteObject(title_font_);if(background_)DeleteObject(background_);}
    void show(int page){page_=std::clamp(page,0,2);scroll_=0;layout();ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);refresh();}
    void refresh(){
        if(!window)return;
        auto s=read();
        const bool enabled=s.ready&&s.fresh&&!s.pending;
        text(11,status_label(s));
        text(12,"Battery "+battery_text(s)+(s.fresh?"":"  ("+checked_time(s.checked)+")"));
        // Retry only appears when reconnection has given up, as in the widget.
        bool retry=!s.ready&&!s.pending&&!s.error.empty();
        if(retry!=retry_visible_){retry_visible_=retry;layout();}
        EnableWindow(control(410),enabled);
        if(!SendMessageW(control(410),CB_GETDROPPEDSTATE,0,0)){
            auto found=std::find(kPresetOrder.begin(),kPresetOrder.end(),s.preset);
            SendMessageW(control(410),CB_SETCURSEL,found==kPresetOrder.end()?static_cast<WPARAM>(-1):static_cast<WPARAM>(found-kPresetOrder.begin()),0);
        }
        text(412,!s.error.empty()?s.error:s.preset==tribit::Custom?"Custom EQ is active. Edit it in the Tribit app; choosing a preset here replaces it.":!s.fresh&&s.preset>=0?"Last preset: "+preset_label(s.preset)+". Presets need a connection.":"");
        for(int i=0;i<5;++i){EnableWindow(control(320+i),enabled&&s.prompts[i]>=0);SendMessageW(control(320+i),BM_SETCHECK,s.prompts[i]==1?BST_CHECKED:BST_UNCHECKED,0);}
        EnableWindow(control(330),enabled&&s.auto_shutdown>=0);SendMessageW(control(330),BM_SETCHECK,s.auto_shutdown==1?BST_CHECKED:BST_UNCHECKED,0);
        bool buttons_known=std::none_of(s.buttons.begin(),s.buttons.end(),[](int v){return v<0;});
        if(!buttons_dirty_)for(int i=0;i<3;++i)if(!SendMessageW(control(350+i),CB_GETDROPPEDSTATE,0,0)){
            auto found=std::find(kPresetOrder.begin(),kPresetOrder.end(),s.buttons[i]);
            SendMessageW(control(350+i),CB_SETCURSEL,found==kPresetOrder.end()?static_cast<WPARAM>(-1):static_cast<WPARAM>(found-kPresetOrder.begin()),0);
        }
        for(int i=0;i<3;++i)EnableWindow(control(350+i),enabled&&buttons_known);
        EnableWindow(control(360),enabled&&buttons_dirty_);InvalidateRect(control(360),nullptr,TRUE);
        SendMessageW(control(314),BM_SETCHECK,s.release_when_hidden?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(control(315),BM_SETCHECK,get_logging()?BST_CHECKED:BST_UNCHECKED,0);
        text(600,"Device: Tribit XSound Plus 2\r\nFirmware: "+(s.firmware.empty()?std::string("Not checked"):s.firmware)+"\r\nAddress: "+(s.address.empty()?std::string("Not checked"):s.address)+
            "\r\nControls: "+s.status+"\r\nBattery: "+battery_text(s)+"\r\n"+checked_time(s.checked)+(s.error.empty()?"":"\r\n\r\n"+s.error));
    }
private:
    struct Placement{HWND hwnd;int page,x,y,width,height,id;};
    HWND viewport_{};
    std::vector<Placement> placements_;
    std::map<int,HWND> handles_;
    HFONT font_{},title_font_{};HBRUSH background_{};
    COLORREF background_color_=RGB(248,249,251),text_color_=RGB(24,24,24),accent_=RGB(0,103,192),button_=RGB(233,235,238);
    float scale_=1;
    int page_{},scroll_{};
    bool dark_{},high_contrast_{},buttons_dirty_{},retry_visible_{};
    HWND control(int id)const{auto found=handles_.find(id);return found==handles_.end()?nullptr:found->second;}
    void text(int id,const std::string& value){SetWindowTextW(control(id),winrt::to_hstring(value).c_str());}
    void add(const wchar_t* cls,const wchar_t* label,int id,int x,int y,int width,int height,DWORD style=0,int page=-1){
        auto hwnd=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,page<0?window:viewport_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        handles_[id]=hwnd;placements_.push_back({hwnd,page,x,y,width,height,id});SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(id==10?title_font_:font_),TRUE);
        if(style&WS_TABSTOP)SetWindowSubclass(hwnd,focus_proc,1,reinterpret_cast<DWORD_PTR>(this));
    }
    void theme(){
        DWORD light=1,text_scale=100,bytes=sizeof(DWORD);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
        bytes=sizeof(DWORD);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Accessibility",L"TextScaleFactor",RRF_RT_REG_DWORD,nullptr,&text_scale,&bytes);
        scale_=static_cast<float>(GetDpiForWindow(window))/96.0f*static_cast<float>(std::clamp<DWORD>(text_scale,100,225))/100.0f;
        HIGHCONTRASTW hc{sizeof(hc)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);high_contrast_=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;dark_=!light&&!high_contrast_;
        background_color_=high_contrast_?GetSysColor(COLOR_WINDOW):dark_?RGB(31,32,35):RGB(248,249,251);
        text_color_=high_contrast_?GetSysColor(COLOR_WINDOWTEXT):dark_?RGB(242,242,245):RGB(24,24,28);
        button_=high_contrast_?GetSysColor(COLOR_BTNFACE):dark_?RGB(52,54,59):RGB(228,231,236);
        DWORD color{};BOOL opaque{};if(SUCCEEDED(DwmGetColorizationColor(&color,&opaque)))accent_=RGB((color>>16)&255,(color>>8)&255,color&255);
        if(high_contrast_)accent_=GetSysColor(COLOR_HIGHLIGHT);
        if(background_)DeleteObject(background_);background_=CreateSolidBrush(background_color_);
        auto old_font=font_,old_title=title_font_;
        font_=CreateFontW(-static_cast<int>(15*scale_),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title_font_=CreateFontW(-static_cast<int>(22*scale_),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(auto& p:placements_){SendMessageW(p.hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(p.id==10?title_font_:font_),TRUE);SetWindowTheme(p.hwnd,L"Explorer",nullptr);}
        if(old_font)DeleteObject(old_font);if(old_title)DeleteObject(old_title);
        BOOL dark=dark_;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        InvalidateRect(window,nullptr,TRUE);if(viewport_)InvalidateRect(viewport_,nullptr,TRUE);
    }
    void layout(){
        if(!viewport_)return;
        RECT client{};GetClientRect(window,&client);int viewport_top=static_cast<int>(172*scale_);
        MoveWindow(viewport_,0,viewport_top,client.right,std::max<int>(1,client.bottom-viewport_top),TRUE);
        RECT view{};GetClientRect(viewport_,&view);int visible=static_cast<int>(view.bottom/scale_);int extent=page_==0?150:page_==1?250:760;
        scroll_=std::clamp(scroll_,0,std::max(0,extent-visible));SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,extent-1,static_cast<UINT>(std::max(1,visible)),scroll_,0};SetScrollInfo(viewport_,SB_VERT,&info,TRUE);
        for(auto& p:placements_){
            bool shown=(p.page<0||p.page==page_)&&(p.id!=103||retry_visible_);ShowWindow(p.hwnd,shown?SW_SHOW:SW_HIDE);if(!shown)continue;
            int width=static_cast<int>((p.page<0?client.right:view.right)/scale_);
            int x=p.x>=0?p.x:width+p.x;int w=p.width>0?p.width:width-x+p.width;
            MoveWindow(p.hwnd,static_cast<int>(x*scale_),static_cast<int>((p.y-(p.page<0?0:scroll_))*scale_),std::max(1,static_cast<int>(w*scale_)),static_cast<int>(p.height*scale_),TRUE);
        }
        for(int id=200;id<=202;++id)InvalidateRect(control(id),nullptr,TRUE);
    }
    void submit(Command command){if(!send(std::move(command)))refresh();}
    void command(int id,int notification){
        if(id>=200&&id<=202){page_=id-200;scroll_=0;layout();return;}
        if(id==103){submit({Operation::Retry});return;}
        if(id==104){ShellExecuteW(window,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(id==410&&notification==CBN_SELCHANGE){auto index=SendMessageW(control(id),CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<kPresetOrder.size())submit({Operation::Preset,kPresetOrder[static_cast<size_t>(index)]});return;}
        if(id>=320&&id<=324){submit({Operation::Prompt,SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED?1:0,id-320});return;}
        if(id==330){submit({Operation::AutoShutdown,SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED?1:0});return;}
        if(id>=350&&id<=352&&notification==CBN_SELCHANGE){buttons_dirty_=true;refresh();return;}
        if(id==360){
            Command apply{Operation::Buttons};
            for(int i=0;i<3;++i){auto index=SendMessageW(control(350+i),CB_GETCURSEL,0,0);if(index<0||static_cast<size_t>(index)>=kPresetOrder.size())return;apply.buttons[i]=kPresetOrder[static_cast<size_t>(index)];}
            buttons_dirty_=false;submit(apply);return;
        }
        if(id==314){set_release(SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED);return;}
        if(id==315){set_logging(SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED);return;}
    }
    void draw_button(DRAWITEMSTRUCT const& draw){
        bool selected=(draw.CtlID>=200&&draw.CtlID<=202&&static_cast<int>(draw.CtlID)==200+page_)||(draw.CtlID==360&&IsWindowEnabled(draw.hwndItem));
        COLORREF fill=selected?accent_:button_;if(draw.itemState&ODS_SELECTED)fill=high_contrast_?GetSysColor(COLOR_HIGHLIGHT):RGB(GetRValue(fill)*4/5,GetGValue(fill)*4/5,GetBValue(fill)*4/5);
        HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,fill);auto old_brush=SelectObject(draw.hDC,brush);auto old_pen=SelectObject(draw.hDC,pen);
        RoundRect(draw.hDC,draw.rcItem.left,draw.rcItem.top,draw.rcItem.right,draw.rcItem.bottom,static_cast<int>(10*scale_),static_cast<int>(10*scale_));SelectObject(draw.hDC,old_brush);SelectObject(draw.hDC,old_pen);DeleteObject(brush);DeleteObject(pen);
        int luminance=GetRValue(fill)*299+GetGValue(fill)*587+GetBValue(fill)*114;
        SetTextColor(draw.hDC,(draw.itemState&ODS_DISABLED)?GetSysColor(COLOR_GRAYTEXT):selected?(luminance>150000?RGB(0,0,0):RGB(255,255,255)):text_color_);SetBkMode(draw.hDC,TRANSPARENT);
        wchar_t caption[128]{};GetWindowTextW(draw.hwndItem,caption,128);RECT rect=draw.rcItem;SelectObject(draw.hDC,font_);DrawTextW(draw.hDC,caption,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw.itemState&ODS_FOCUS){InflateRect(&rect,-4,-4);DrawFocusRect(draw.hDC,&rect);}
    }
    void scroll(int code){RECT view{};GetClientRect(viewport_,&view);SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(viewport_,SB_VERT,&info);int page=static_cast<int>(view.bottom/scale_);
        if(code==SB_LINEUP)scroll_-=32;else if(code==SB_LINEDOWN)scroll_+=32;else if(code==SB_PAGEUP)scroll_-=page;else if(code==SB_PAGEDOWN)scroll_+=page;else if(code==SB_THUMBTRACK)scroll_=info.nTrackPos;layout();}
    static LRESULT CALLBACK focus_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<NativeEditor*>(data);
        if(message==WM_SETFOCUS){for(auto& p:self->placements_)if(p.hwnd==hwnd&&p.page>=0){RECT view{};GetClientRect(self->viewport_,&view);int height=static_cast<int>(view.bottom/self->scale_);if(p.y<self->scroll_)self->scroll_=p.y;if(p.y+p.height>self->scroll_+height)self->scroll_=p.y+p.height-height;self->layout();break;}}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,focus_proc,id);
        return DefSubclassProc(hwnd,message,wparam,lparam);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<NativeEditor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<NativeEditor*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wparam,lparam);
        try{
            if(message==WM_ERASEBKGND){RECT area{};GetClientRect(hwnd,&area);FillRect(reinterpret_cast<HDC>(wparam),&area,self->background_);return 1;}
            if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN){auto dc=reinterpret_cast<HDC>(wparam);SetTextColor(dc,self->text_color_);SetBkColor(dc,self->background_color_);return reinterpret_cast<LRESULT>(self->background_);}
            if(message==WM_DRAWITEM){self->draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));return TRUE;}
            if(message==WM_COMMAND){self->command(LOWORD(wparam),HIWORD(wparam));return 0;}
            if(message==WM_SIZE&&hwnd==self->window){self->layout();return 0;}
            if(message==WM_VSCROLL&&hwnd==self->viewport_){self->scroll(LOWORD(wparam));return 0;}
            if(message==WM_MOUSEWHEEL){self->scroll(GET_WHEEL_DELTA_WPARAM(wparam)>0?SB_LINEUP:SB_LINEDOWN);return 0;}
            if(message==WM_SETTINGCHANGE){self->theme();self->layout();return 0;}
            if(message==WM_DPICHANGED){auto* rect=reinterpret_cast<RECT*>(lparam);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);self->theme();self->layout();return 0;}
            if(message==WM_GETMINMAXINFO){auto* info=reinterpret_cast<MINMAXINFO*>(lparam);info->ptMinTrackSize={static_cast<LONG>(490*self->scale_),static_cast<LONG>(400*self->scale_)};return 0;}
            if(message==WM_CLOSE&&hwnd==self->window){auto s=self->read();if((self->buttons_dirty_||(s.pending&&s.status!="Connecting..."))&&MessageBoxW(hwnd,L"A setting change is unapplied or unresolved. Close controls anyway?",L"Tribit Connect",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
                DestroyWindow(hwnd);self->window=nullptr;self->closed();return 0;}
        }catch(const std::exception& e){MessageBoxW(self->window,winrt::to_hstring(e.what()).c_str(),L"Tribit Connect",MB_ICONERROR);}catch(const winrt::hresult_error& e){MessageBoxW(self->window,e.message().c_str(),L"Tribit Connect",MB_ICONERROR);}
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
};
