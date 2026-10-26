#pragma once
#include "widget_state.hpp"
#include "widget_images.hpp"
#include <functional>

// Everything a card can show, built the same way for the Widgets host and the mock.
inline constexpr std::array<const char*,11> kSymbolNames{"settings","equalizer","back","previous","next","bluetooth","link","diagnostics","log","bell","power"};

// `symbol` turns an exported icon file name into the URL the card should use.
static void load_artwork(WidgetIcons& icons,const std::function<std::string(const std::string&)>& symbol,bool high,bool white_text){
    for(auto name:kSymbolNames){
        std::string light=std::string(name)+(high?(white_text?"-hc-dark.png":"-hc-light.png"):"-light.png");
        std::string dark=std::string(name)+(high?(white_text?"-hc-dark.png":"-hc-light.png"):"-dark.png");
        icons.symbols[name]={symbol(light),symbol(dark)};
    }
    icons.curves.clear();icons.custom_curves.clear();
    for(auto& curve:kPresetCurves)for(int height:kCurveHeights)
        icons.curves[{curve.preset,height}]={curve_image(curve,false,high,268,height),curve_image(curve,true,high,268,height)};}
// A curve saved in Sound Lab, drawn like the presets: ±8 dB fills the presets' shared scale.
static PresetCurve band_curve(const Bands& bands){
    PresetCurve curve{tribit::Custom,RGB(0xFF,0x5A,0x14),{}};const int n=static_cast<int>(bands.size()),samples=static_cast<int>(curve.heights.size());
    for(int k=0;k<samples;++k){
        float t=static_cast<float>(k)*(n-1)/(samples-1);int i=std::min(static_cast<int>(t),n-2);float u=t-i;
        auto at=[&](int j){return static_cast<float>(bands[std::clamp(j,0,n-1)]);};
        float p0=at(i-1),p1=at(i),p2=at(i+1),p3=at(i+2);
        float v=0.5f*((2*p1)+(-p0+p2)*u+(2*p0-5*p1+4*p2-p3)*u*u+(-p0+3*p1-3*p2+p3)*u*u*u);
        curve.heights[k]=static_cast<int>(std::lround(580+std::clamp(v,-8.f,8.f)*25));
    }
    return curve;
}
// Draws a saved curve once per height; the cache stays small (six buttons plus what plays).
static void custom_artwork(WidgetIcons& icons,const Bands& bands,bool high){
    if(icons.custom_curves.size()>48)icons.custom_curves.clear();
    for(int height:kCurveHeights)if(!icons.custom_curves.contains({bands,height})){
        auto curve=band_curve(bands);icons.custom_curves[{bands,height}]={curve_image(curve,false,high,268,height),curve_image(curve,true,high,268,height)};}
}
