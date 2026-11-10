// The updater's decisions (src/updates.hpp): which release counts, and which download is trusted.
#include "updates.hpp"
#include <iostream>

using namespace tribit::updates;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}

int main(){try{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    expect(parse_version(L"0.7.0")==std::array<int,3>{0,7,0}&&parse_version(L"v1.12.3")==std::array<int,3>{1,12,3},"Versions parse with or without v");
    expect(!parse_version(L"0.7")&&!parse_version(L"0.7.0.0")&&!parse_version(L"0.7.x")&&!parse_version(L""),"Anything but MAJOR.MINOR.PATCH is rejected");
    const std::wstring base=L"https://github.com/KanuTomer/tribit-connect/releases/download/v0.8.0/";
    auto release=[&](const std::wstring& tag,const std::wstring& extra=L""){
        return winrt::hstring(L"{\"tag_name\":\""+tag+L"\",\"draft\":false,\"assets\":[{\"name\":\"TribitConnect-Setup-0.8.0.exe\",\"browser_download_url\":\""+base+L"TribitConnect-Setup-0.8.0.exe\"},"
            L"{\"name\":\"TribitConnect-Setup-0.8.0.exe.sha256\",\"browser_download_url\":\""+base+L"TribitConnect-Setup-0.8.0.exe.sha256\"}]"+extra+L"}");};
    auto r=newer_release(release(L"v0.8.0"),{0,7,0});
    expect(r&&r->version==L"0.8.0"&&r->setup_url==base+L"TribitConnect-Setup-0.8.0.exe"&&r->sha_url.ends_with(L".sha256"),"A newer release with both files is offered");
    expect(!newer_release(release(L"v0.8.0"),{0,8,0})&&!newer_release(release(L"v0.8.0"),{0,9,1}),"The same or an older release is not");
    expect(!newer_release(release(L"v0.8.0",L",\"prerelease\":true"),{0,7,0}),"Pre-releases are not offered");
    expect(!newer_release(winrt::hstring(L"{\"tag_name\":\"v0.8.0\",\"assets\":[{\"name\":\"TribitConnect-Setup-0.8.0.exe\",\"browser_download_url\":\""+base+L"TribitConnect-Setup-0.8.0.exe\"}]}"),{0,7,0}),"A release without its checksum is not offered");
    expect(!newer_release(winrt::hstring(L"{\"tag_name\":\"v0.8.0\",\"assets\":[{\"name\":\"TribitConnect-Setup-0.8.0.exe\",\"browser_download_url\":\"https://example.com/TribitConnect-Setup-0.8.0.exe\"},"
        L"{\"name\":\"TribitConnect-Setup-0.8.0.exe.sha256\",\"browser_download_url\":\"https://example.com/x.sha256\"}]}"),{0,7,0}),"Files from anywhere but this repository's releases are ignored");
    expect(!newer_release(L"not json",{0,7,0}),"A broken reply offers nothing");
    expect(expected_sha(L"ab"+std::wstring(62,L'0')+L"  TribitConnect-Setup-0.8.0.exe\r\n")==L"AB"+std::wstring(62,L'0')&&expected_sha(L"xyz  file").empty(),"The checksum file's hash is read and checked for shape");
    // A file that is not signed by this project never passes, even when its hash matches.
    auto path=std::filesystem::temp_directory_path()/L"tribit-update-test.exe";
    {std::ofstream(path,std::ios::binary)<<"not a signed setup";}
    expect(!verified(path,file_sha256(path))&&!verified(path,L""),"An unsigned download is refused");
    std::error_code ec;std::filesystem::remove(path,ec);
    std::cout<<"Passed: version parsing, release choice, checksum and signature checks.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
