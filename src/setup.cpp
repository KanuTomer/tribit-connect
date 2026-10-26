// TribitConnect-Setup.exe: one download that installs Tribit Connect.
//
// The MSIX and its signing certificate are embedded as resources. Setup runs as the user and:
//   1. trusts the certificate (LocalMachine\TrustedPeople) if it is not trusted yet; only this step
//      needs administrator rights, so setup relaunches itself elevated with --trust for it;
//   2. installs the Windows App Runtime the package depends on, if missing, from Microsoft's
//      official pinned installer;
//   3. installs (or updates) the package for the current user, and offers to open the app.
// `--check <file>` writes what it would do, without changing anything (used for testing).
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <urlmon.h>
#include <wincrypt.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.System.h>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using namespace winrt::Windows::Management::Deployment;

static constexpr wchar_t kApp[]=L"Tribit Connect";
static constexpr wchar_t kAppFamily[]=L"Kanu.TribitConnect_3k1b1c9z5y0mg";
static constexpr wchar_t kRuntimeFamily[]=L"Microsoft.WindowsAppRuntime.2_8wekyb3d8bbwe";
static constexpr UINT64 kRuntimeMin=(2ull<<48)|(5ull<<32)|(1ull<<16);   // 2.5.1.0, as the package manifest requires
static constexpr wchar_t kRuntimeUrl[]=L"https://aka.ms/windowsappsdk/2.5/2.5.1/windowsappruntimeinstall-x64.exe";
enum { kPayloadMsix=101, kPayloadCer=102, kOpen=100 };

static std::vector<BYTE> resource(int id){
    HRSRC r=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);if(!r)throw std::runtime_error("This setup file is damaged.");
    auto* data=static_cast<const BYTE*>(LockResource(LoadResource(nullptr,r)));return {data,data+SizeofResource(nullptr,r)};
}
static void write(const fs::path& path,const std::vector<BYTE>& bytes){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!out)throw std::runtime_error("Could not unpack the installer files.");
}
// Is the publisher certificate already trusted for package installs?
static bool trusted(const std::vector<BYTE>& cer){
    PCCERT_CONTEXT cert=CertCreateCertificateContext(X509_ASN_ENCODING,cer.data(),static_cast<DWORD>(cer.size()));if(!cert)return false;
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE|CERT_STORE_READONLY_FLAG|CERT_STORE_OPEN_EXISTING_FLAG,L"TrustedPeople");
    bool found=false;if(store){PCCERT_CONTEXT hit=CertFindCertificateInStore(store,X509_ASN_ENCODING,0,CERT_FIND_EXISTING,cert,nullptr);found=hit!=nullptr;if(hit)CertFreeCertificateContext(hit);CertCloseStore(store,0);}
    CertFreeCertificateContext(cert);return found;
}
// The elevated half: add the certificate to LocalMachine\TrustedPeople.
static int trust(const fs::path& cer_path){
    std::ifstream in(cer_path,std::ios::binary);std::vector<BYTE> cer{std::istreambuf_iterator<char>(in),{}};
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"TrustedPeople");if(!store)return 2;
    BOOL ok=CertAddEncodedCertificateToStore(store,X509_ASN_ENCODING,cer.data(),static_cast<DWORD>(cer.size()),CERT_STORE_ADD_REPLACE_EXISTING,nullptr);
    CertCloseStore(store,0);return ok?0:3;
}
static bool runtime_present(){
    for(auto package:PackageManager().FindPackagesForUser(L"",kRuntimeFamily)){
        auto id=package.Id();auto v=id.Version();
        UINT64 version=(UINT64(v.Major)<<48)|(UINT64(v.Minor)<<32)|(UINT64(v.Build)<<16)|v.Revision;
        if(id.Architecture()==winrt::Windows::System::ProcessorArchitecture::X64&&version>=kRuntimeMin)return true;
    }
    return false;
}
static std::wstring installed_version(){
    for(auto package:PackageManager().FindPackagesForUser(L"",kAppFamily)){auto v=package.Id().Version();
        return std::to_wstring(v.Major)+L"."+std::to_wstring(v.Minor)+L"."+std::to_wstring(v.Build)+(package.IsDevelopmentMode()?L" (development registration)":L"");}
    return L"";
}
static DWORD run_and_wait(const std::wstring& file,const std::wstring& args,bool elevated,HWND owner){
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.hwnd=owner;
    info.lpVerb=elevated?L"runas":L"open";info.lpFile=file.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info))return GetLastError()==ERROR_CANCELLED?ERROR_CANCELLED:GetLastError();
    WaitForSingleObject(info.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);return code;
}

// ---- the dialog: one progress page, then a result page -------------------------------------------
struct Setup {
    HWND dialog{};std::wstring error;bool done{};
    fs::path folder=fs::temp_directory_path()/L"TribitConnectSetup";
    void step(const wchar_t* text){if(dialog)SendMessageW(dialog,TDM_SET_ELEMENT_TEXT,TDE_CONTENT,reinterpret_cast<LPARAM>(text));}
    void work(){
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            fs::create_directories(folder);
            auto cer=resource(kPayloadCer);auto cer_path=folder/L"TribitConnect.cer",msix=folder/L"TribitConnect.msix";write(cer_path,cer);
            if(!trusted(cer)){
                step(L"Trusting the app's publisher certificate. Windows will ask for permission.");
                wchar_t self[MAX_PATH]{};GetModuleFileNameW(nullptr,self,MAX_PATH);
                DWORD code=run_and_wait(self,L"--trust \""+cer_path.wstring()+L"\"",true,dialog);
                if(code==ERROR_CANCELLED)throw std::runtime_error("Setup needs permission to trust the app's publisher certificate. Run it again and choose Yes.");
                if(code!=0||!trusted(cer))throw std::runtime_error("The publisher certificate could not be trusted.");
            }
            if(!runtime_present()){
                step(L"Downloading the Windows App Runtime from Microsoft…");
                auto exe=folder/L"windowsappruntimeinstall-x64.exe";
                if(FAILED(URLDownloadToFileW(nullptr,kRuntimeUrl,exe.c_str(),0,nullptr)))throw std::runtime_error("Could not download the Windows App Runtime. Check the internet connection and run setup again.");
                step(L"Installing the Windows App Runtime…");
                if(run_and_wait(exe.wstring(),L"--quiet",false,dialog)!=0&&!runtime_present())throw std::runtime_error("The Windows App Runtime could not be installed.");
            }
            step(L"Installing Tribit Connect…");
            write(msix,resource(kPayloadMsix));
            auto result=PackageManager().AddPackageAsync(winrt::Windows::Foundation::Uri(L"file:///"+msix.generic_wstring()),nullptr,
                DeploymentOptions::ForceApplicationShutdown|DeploymentOptions::ForceUpdateFromAnyVersion).get();
            if(FAILED(result.ExtendedErrorCode()))throw std::runtime_error(winrt::to_string(result.ErrorText()));
        }catch(const winrt::hresult_error& e){error=e.message().c_str();}
        catch(const std::exception& e){error=winrt::to_hstring(e.what()).c_str();}
        std::error_code ec;fs::remove_all(folder,ec);
        done=true;finish();
    }
    void finish();
};
static Setup app;
static HRESULT CALLBACK result_proc(HWND,UINT msg,WPARAM wp,LPARAM,LONG_PTR){
    if(msg==TDN_BUTTON_CLICKED&&wp==kOpen)ShellExecuteW(nullptr,L"open",(std::wstring(L"shell:AppsFolder\\")+kAppFamily+L"!App").c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    return S_OK;
}
void Setup::finish(){
    static TASKDIALOG_BUTTON open{kOpen,L"Open Tribit Connect"};
    static TASKDIALOGCONFIG page{sizeof(page)};static std::wstring content;
    page.hwndParent=nullptr;page.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;page.pszWindowTitle=kApp;page.pfCallback=result_proc;
    if(error.empty()){
        page.pszMainIcon=TD_INFORMATION_ICON;page.pszMainInstruction=L"Tribit Connect is installed";
        content=L"Open it from Start, and add the XSound Plus 2 widget from Win + W > Add widgets.";
        page.cButtons=1;page.pButtons=&open;page.nDefaultButton=kOpen;page.dwCommonButtons=TDCBF_CLOSE_BUTTON;
    }else{
        page.pszMainIcon=TD_ERROR_ICON;page.pszMainInstruction=L"Tribit Connect was not installed";content=error;
        page.cButtons=0;page.pButtons=nullptr;page.dwCommonButtons=TDCBF_CLOSE_BUTTON;
    }
    page.pszContent=content.c_str();
    SendMessageW(dialog,TDM_NAVIGATE_PAGE,0,reinterpret_cast<LPARAM>(&page));
}
static HRESULT CALLBACK progress_proc(HWND hwnd,UINT msg,WPARAM,LPARAM,LONG_PTR){
    if(msg==TDN_CREATED){app.dialog=hwnd;SendMessageW(hwnd,TDM_SET_PROGRESS_BAR_MARQUEE,TRUE,0);
        SendMessageW(hwnd,TDM_ENABLE_BUTTON,IDCANCEL,FALSE);std::thread([]{app.work();}).detach();}
    return S_OK;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3&&std::wstring(argv[1])==L"--trust")return trust(argv[2]);
    if(argc==3&&std::wstring(argv[1])==L"--check"){
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::wofstream out(argv[2]);
        try{auto msix=resource(kPayloadMsix);auto cer=resource(kPayloadCer);
            out<<L"payload msix bytes="<<msix.size()<<L" cer bytes="<<cer.size()<<L"\n";
            out<<L"certificate trusted="<<trusted(cer)<<L"\nruntime 2.5.1+ present="<<runtime_present()<<L"\ninstalled version="<<installed_version()<<L"\n";
        }catch(const std::exception& e){out<<L"error "<<e.what()<<L"\n";}
        return 0;
    }
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_PROGRESS_CLASS};InitCommonControlsEx(&icc);
    TASKDIALOGCONFIG page{sizeof(page)};
    page.dwFlags=TDF_SHOW_MARQUEE_PROGRESS_BAR|TDF_SIZE_TO_CONTENT|TDF_CALLBACK_TIMER;page.pszWindowTitle=kApp;
    page.pszMainIcon=MAKEINTRESOURCEW(1);page.hInstance=GetModuleHandleW(nullptr);
    page.pszMainInstruction=L"Installing Tribit Connect";page.pszContent=L"Getting ready…";
    page.dwCommonButtons=TDCBF_CANCEL_BUTTON;page.pfCallback=progress_proc;
    TaskDialogIndirect(&page,nullptr,nullptr,nullptr);
    return app.error.empty()?0:1;
}
