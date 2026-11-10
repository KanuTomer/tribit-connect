#pragma once
// Update checks against this project's GitHub Releases (ported from Bandwave's ui/updates.hpp).
// At most once a day, and only while "Check for updates" is on; Check now looks straight away.
// Nothing about the user or the PC is sent. A newer setup is run only after its SHA-256 matches the
// published .sha256, its Authenticode signature is intact, and it is signed by this project's
// certificate (CN=TribitConnect.Local).
#include <windows.h>
#include <appmodel.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Storage.Streams.h>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace tribit::updates {
inline constexpr wchar_t kLatest[]=L"https://api.github.com/repos/KanuTomer/tribit-connect/releases/latest";
inline constexpr wchar_t kDownloads[]=L"https://github.com/KanuTomer/tribit-connect/releases/download/";
inline constexpr wchar_t kThumbprint[]=L"B0ADF85FA550EE5D6060A81E94E073A3D97B9BFA";
inline constexpr wchar_t kAgent[]=L"TribitConnect";

inline int64_t now_seconds(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}

// "0.7.0" from the package, or empty for a build that is not installed as a package.
inline std::wstring app_version(){
    UINT32 length=0;
    if(GetCurrentPackageFullName(&length,nullptr)==APPMODEL_ERROR_NO_PACKAGE)return L"";
    try{auto v=winrt::Windows::ApplicationModel::Package::Current().Id().Version();
        return std::to_wstring(v.Major)+L"."+std::to_wstring(v.Minor)+L"."+std::to_wstring(v.Build);}
    catch(...){return L"";}
}

// "0.5.1" or "v0.5.1" -> {0,5,1}; anything else is rejected.
inline std::optional<std::array<int,3>> parse_version(std::wstring text){
    if(!text.empty()&&(text[0]==L'v'||text[0]==L'V'))text.erase(0,1);
    std::array<int,3> v{};size_t at=0;
    for(int part=0;part<3;++part){
        size_t used=0;
        if(at>=text.size()||!iswdigit(text[at]))return std::nullopt;
        try{v[static_cast<size_t>(part)]=std::stoi(text.substr(at),&used);}catch(...){return std::nullopt;}
        at+=used;
        if(part<2){if(at>=text.size()||text[at]!=L'.')return std::nullopt;++at;}
    }
    return at==text.size()?std::optional{v}:std::nullopt;
}

struct Release{std::wstring version,setup_url,sha_url;};

inline winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> latest_json(){
    // Always ask GitHub: Windows' HTTP cache would otherwise answer a second Check now from the first reply.
    winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter filter;
    filter.CacheControl().ReadBehavior(winrt::Windows::Web::Http::Filters::HttpCacheReadBehavior::NoCache);
    winrt::Windows::Web::Http::HttpClient client{filter};
    client.DefaultRequestHeaders().UserAgent().ParseAdd(kAgent);
    client.DefaultRequestHeaders().Accept().ParseAdd(L"application/vnd.github+json");
    co_return co_await client.GetStringAsync(winrt::Windows::Foundation::Uri(kLatest));
}
// The newest release, if it is newer than `current` and carries a setup and its checksum.
inline std::optional<Release> newer_release(winrt::hstring const& body,std::array<int,3> current){
    namespace json=winrt::Windows::Data::Json;
    json::JsonObject root{nullptr};
    if(!json::JsonObject::TryParse(body,root))return std::nullopt;
    auto version=parse_version(std::wstring(root.GetNamedString(L"tag_name",L"")));
    if(!version||*version<=current||root.GetNamedBoolean(L"draft",false)||root.GetNamedBoolean(L"prerelease",false))return std::nullopt;
    Release release;
    release.version=std::to_wstring((*version)[0])+L"."+std::to_wstring((*version)[1])+L"."+std::to_wstring((*version)[2]);
    const std::wstring setup=L"TribitConnect-Setup-"+release.version+L".exe";
    auto assets=root.GetNamedArray(L"assets",json::JsonArray());
    for(uint32_t i=0;i<assets.Size();++i){
        if(assets.GetAt(i).ValueType()!=json::JsonValueType::Object)continue;
        auto asset=assets.GetObjectAt(i);
        const std::wstring name{asset.GetNamedString(L"name",L"")},url{asset.GetNamedString(L"browser_download_url",L"")};
        if(!url.starts_with(kDownloads))continue;
        if(name==setup)release.setup_url=url;
        if(name==setup+L".sha256")release.sha_url=url;
    }
    if(release.setup_url.empty()||release.sha_url.empty())return std::nullopt;
    return release;
}

inline std::wstring file_sha256(std::filesystem::path const& path){
    std::ifstream in(path,std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(in),{}};
    BCRYPT_ALG_HANDLE algorithm{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)return {};
    std::array<UCHAR,32> digest{};
    BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),digest.data(),32);
    BCryptCloseAlgorithmProvider(algorithm,0);
    std::wstring hex;for(auto b:digest){wchar_t pair[3];swprintf_s(pair,L"%02X",b);hex+=pair;}
    return hex;
}
// The signature must be intact (a tampered file fails with a bad digest), and its signer must be this
// project's certificate. The certificate is self-signed, so an untrusted-root result is fine.
inline bool signed_by_us(std::filesystem::path const& file){
    WINTRUST_FILE_INFO info{sizeof(info)};info.pcwszFilePath=file.c_str();
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&info;data.dwStateAction=WTD_STATEACTION_VERIFY;
    const LONG status=WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data);
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data);
    if(status!=ERROR_SUCCESS&&status!=CERT_E_UNTRUSTEDROOT&&status!=CERT_E_CHAINING)return false;
    HCERTSTORE store{};HCRYPTMSG message{};DWORD encoding{},content{},format{};
    if(!CryptQueryObject(CERT_QUERY_OBJECT_FILE,file.c_str(),CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,CERT_QUERY_FORMAT_FLAG_BINARY,0,&encoding,&content,&format,&store,&message,nullptr))return false;
    bool match=false;DWORD size=0;
    if(CryptMsgGetParam(message,CMSG_SIGNER_INFO_PARAM,0,nullptr,&size)){
        std::string buffer(size,'\0');
        if(CryptMsgGetParam(message,CMSG_SIGNER_INFO_PARAM,0,buffer.data(),&size)){
            auto signer=reinterpret_cast<PCMSG_SIGNER_INFO>(buffer.data());
            CERT_INFO wanted{};wanted.Issuer=signer->Issuer;wanted.SerialNumber=signer->SerialNumber;
            if(auto cert=CertFindCertificateInStore(store,X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,0,CERT_FIND_SUBJECT_CERT,&wanted,nullptr)){
                BYTE hash[20]{};DWORD length=sizeof(hash);
                if(CertGetCertificateContextProperty(cert,CERT_HASH_PROP_ID,hash,&length)){
                    std::wstring hex;for(DWORD i=0;i<length;++i){wchar_t pair[3];swprintf_s(pair,L"%02X",hash[i]);hex+=pair;}
                    match=hex==kThumbprint;
                }
                CertFreeCertificateContext(cert);
            }
        }
    }
    CryptMsgClose(message);CertCloseStore(store,0);
    return match;
}
// The checksum file's first word, upper-case, if it looks like a SHA-256.
inline std::wstring expected_sha(std::wstring text){
    text=text.substr(0,text.find_first_of(L" \t\r\n"));
    for(auto& c:text)c=static_cast<wchar_t>(towupper(c));
    return text.size()==64&&text.find_first_not_of(L"0123456789ABCDEF")==std::wstring::npos?text:L"";
}
// A downloaded setup is kept only if every check passes.
inline bool verified(std::filesystem::path const& path,std::wstring const& expected){
    return !expected.empty()&&file_sha256(path)==expected&&signed_by_us(path);
}
// Downloads the setup and its checksum to a temporary folder; the setup's path, or empty if a check failed.
inline winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> download(Release release){
    winrt::Windows::Web::Http::HttpClient client;
    client.DefaultRequestHeaders().UserAgent().ParseAdd(kAgent);
    auto expected=expected_sha(std::wstring(co_await client.GetStringAsync(winrt::Windows::Foundation::Uri(release.sha_url))));
    if(expected.empty())co_return L"";
    auto buffer=co_await client.GetBufferAsync(winrt::Windows::Foundation::Uri(release.setup_url));
    const auto folder=std::filesystem::temp_directory_path()/L"TribitConnectUpdate";
    std::error_code ec;std::filesystem::create_directories(folder,ec);
    const auto path=folder/(L"TribitConnect-Setup-"+release.version+L".exe");
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(buffer.data()),buffer.Length());}
    if(!verified(path,expected)){std::filesystem::remove(path,ec);co_return L"";}
    co_return winrt::hstring(path.wstring());
}
}
