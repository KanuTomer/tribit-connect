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
    icons.curves.clear();
    for(auto& curve:kPresetCurves)for(int height:kCurveHeights)
        icons.curves[{curve.preset,height}]={curve_image(curve,false,high,268,height),curve_image(curve,true,high,268,height)};}
