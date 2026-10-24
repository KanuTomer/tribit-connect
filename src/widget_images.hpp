#pragma once
#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

static std::string png_uri(std::span<const uint8_t> bytes){
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result="data:image/png;base64,";uint32_t buffer{};int bits{};
    for(auto value:bytes){buffer=(buffer<<8)|value;bits+=8;while(bits>=6){bits-=6;result+=alphabet[(buffer>>bits)&63];}}
    if(bits)result+=alphabet[(buffer<<(6-bits))&63];while((result.size()-22)%4)result+='=';return result;
}
static std::string bitmap_uri(int width,int height,std::span<const uint8_t> pixels){
    winrt::com_ptr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));
    winrt::com_ptr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()));winrt::check_hresult(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(frame.put(),nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(width,height));
    auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder changed the pixel format.");
    winrt::check_hresult(frame->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
    STATSTG stat{};winrt::check_hresult(stream->Stat(&stat,STATFLAG_NONAME));if(stat.cbSize.QuadPart>65536)throw std::runtime_error("Widget artwork exceeded its size limit.");
    HGLOBAL global{};winrt::check_hresult(GetHGlobalFromStream(stream.get(),&global));auto* bytes=static_cast<uint8_t*>(GlobalLock(global));if(!bytes)winrt::throw_last_error();
    auto result=png_uri({bytes,static_cast<size_t>(stat.cbSize.QuadPart)});GlobalUnlock(global);return result;
}
// Preset EQ curves, traced from the Tribit app's Equalizer graph on 25 October 2026; the
// speaker itself reports no band values for built-in presets. Heights are 0..1000 on one
// shared scale, sampled at 33 evenly spaced points from the lowest to the highest band.
struct PresetCurve{int preset;COLORREF color;std::array<int,33> heights;};
inline constexpr std::array<PresetCurve,6> kPresetCurves{{
    {0,RGB(0xEA,0xC0,0x39),{472,565,643,704,746,748,713,652,583,535,513,498,491,487,480,474,467,463,461,470,502,548,583,593,589,576,565,559,561,578,604,639,678}},
    {1,RGB(0x2B,0xAF,0xAA),{465,546,611,663,698,700,676,628,574,535,511,493,483,476,470,465,461,461,461,463,480,504,522,526,524,517,511,509,509,517,533,548,567}},
    {3,RGB(0xFF,0xE9,0x00),{411,450,485,522,559,604,654,691,704,704,704,704,704,704,704,704,704,704,704,704,702,689,648,598,557,524,489,467,459,459,459,459,459}},
    {4,RGB(0x6D,0x2A,0xC7),{580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,593,626,663,691,709,720,726,728,728,728,728,728}},
    {2,RGB(0xDB,0x9C,0x37),{461,461,461,461,461,467,528,626,715,752,752,752,752,752,752,752,752,752,752,746,715,663,613,578,548,524,504,489,476,470,465,463,461}},
    {5,RGB(0x9B,0x6B,0x31),{726,672,628,596,580,604,657,709,728,728,728,728,728,707,654,602,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580}},
}};
// Draws one preset's curve the way the app does: a smooth line over a fill that fades out.
static std::string curve_image(const PresetCurve& curve,bool dark,bool high,int logical_width=268,int logical_height=72){
    constexpr int scale=2;const int width=logical_width*scale,height=logical_height*scale;
    std::vector<float> line(width);
    const auto& h=curve.heights;const int n=static_cast<int>(h.size());
    for(int x=0;x<width;++x){
        // Catmull-Rom between traced samples keeps the app's rounded shoulders.
        float t=static_cast<float>(x)*(n-1)/(width-1);int i=std::min(static_cast<int>(t),n-2);float u=t-i;
        auto at=[&](int k){return static_cast<float>(h[std::clamp(k,0,n-1)]);};
        float p0=at(i-1),p1=at(i),p2=at(i+1),p3=at(i+2);
        float v=0.5f*((2*p1)+(-p0+p2)*u+(2*p0-5*p1+4*p2-p3)*u*u+(-p0+3*p1-3*p2+p3)*u*u*u);
        // Shared vertical scale for every preset, so curves compare at a glance.
        line[x]=(780.f-v)/(780.f-380.f)*(height-8*scale)+4*scale;
    }
    COLORREF color=high?GetSysColor(COLOR_WINDOWTEXT):curve.color;
    // Light hosts need darker strokes for the yellow presets to stay visible.
    if(!dark&&!high)color=RGB(GetRValue(color)*7/10,GetGValue(color)*7/10,GetBValue(color)*7/10);
    std::vector<uint8_t> pixels(width*height*4);
    const float thickness=1.6f*scale;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        float distance=std::abs(y+0.5f-line[x]);
        if(x>0)distance=std::min(distance,std::abs(y+0.5f-(line[x]+line[x-1])/2));
        float ink=std::clamp(thickness/2+0.5f-distance,0.f,1.f);
        float fill=high||y<line[x]?0.f:0.38f*(1.f-(y-line[x])/(height-line[x]+1));
        float alpha=std::max(ink,fill);if(alpha<=0)continue;
        auto at=(y*width+x)*4;pixels[at]=GetBValue(color);pixels[at+1]=GetGValue(color);pixels[at+2]=GetRValue(color);pixels[at+3]=static_cast<uint8_t>(alpha*255+.5f);
    }
    return bitmap_uri(width,height,pixels);
}
// Preset button colours, taken from the Tribit app's preset cards.
inline constexpr std::array<std::pair<int,COLORREF>,6> kPresetTileColors{{
    {0,RGB(0xE8,0xBE,0x38)},{1,RGB(0x2B,0xAF,0xAA)},{3,RGB(0xB9,0x5D,0x1F)},{4,RGB(0x6D,0x2A,0xC7)},{2,RGB(0xDB,0x9C,0x37)},{5,RGB(0x9B,0x6B,0x31)}}};
// A translucent 1x1 wash laid over the selected preset's emphasis tile, so its focus still shows.
// Light hosts get a paler wash so their dark text stays readable.
static std::string preset_fill(COLORREF color,bool dark){
    double alpha=dark?0.78:0.50;
    std::array<uint8_t,4> pixel{GetBValue(color),GetGValue(color),GetRValue(color),static_cast<uint8_t>(alpha*255+.5)};
    return bitmap_uri(1,1,pixel);
}
// Small battery glyph: outline, terminal nub and a fill that follows the level.
static std::string battery_meter(int value,bool dark,bool high){
    constexpr int scale=3,logical_width=28,logical_height=14,width=logical_width*scale,height=logical_height*scale;
    bool valid=value>=0&&value<=100,low=valid&&value<=20;
    auto ink=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(224,224,224):RGB(64,64,64);
    auto fill=high?GetSysColor(COLOR_WINDOWTEXT):low?(dark?RGB(255,128,120):RGB(196,43,28)):ink;
    std::vector<uint8_t> pixels(width*height*4);
    auto put=[&](int x,int y,COLORREF color,float alpha){if(x<0||y<0||x>=width||y>=height||alpha<=0)return;auto at=(y*width+x)*4;
        pixels[at]=GetBValue(color);pixels[at+1]=GetGValue(color);pixels[at+2]=GetRValue(color);pixels[at+3]=static_cast<uint8_t>(std::min(1.f,alpha)*255+.5f);};
    // Body 24x12 with 2px corner radius, nub 2x6, in logical pixels.
    const float left=1.f*scale,top=1.f*scale,right=25.f*scale,bottom=13.f*scale,radius=2.5f*scale,stroke=1.4f*scale;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        float px=x+.5f,py=y+.5f;
        float cx=std::clamp(px,left+radius,right-radius),cy=std::clamp(py,top+radius,bottom-radius);
        float d=std::sqrt((px-cx)*(px-cx)+(py-cy)*(py-cy))-radius; // signed distance to the rounded body
        float edge=std::clamp(stroke/2+.5f-std::abs(d+stroke/2),0.f,1.f);
        if(edge>0){put(x,y,ink,valid?edge:edge*.45f);continue;}
        float inner=left+2.2f*scale,span=(right-2.2f*scale)-inner;
        if(valid&&d<-stroke-0.8f*scale&&px<inner+span*value/100.f)put(x,y,fill,1.f);
        if(px>=right+0.6f*scale&&px<=right+2.4f*scale&&py>=5.f*scale&&py<=9.f*scale)put(x,y,ink,valid?1.f:.45f);
    }
    return bitmap_uri(width,height,pixels);
}
