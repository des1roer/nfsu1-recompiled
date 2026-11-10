#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
struct AndroidRenderMode {uint32_t width,height;};
inline std::array<AndroidRenderMode,6> androidRenderModes(uint32_t width,uint32_t height,uint32_t initialWidth,uint32_t initialHeight) {
    auto scaled=[&](uint32_t percent){return AndroidRenderMode{
        std::max(320u,(width*percent/100u)&~1u),std::max(240u,(height*percent/100u)&~1u)};};
    return {scaled(25),scaled(50),scaled(75),AndroidRenderMode{width,height},
        AndroidRenderMode{1280,720},AndroidRenderMode{initialWidth,initialHeight}};
}
// "4:3 with bars": the largest 4:3 box centred in a surface of this size. The
// game's original geometry is drawn there instead of being stretched to the panel.
struct AndroidBox {int32_t left,top;uint32_t width,height;};
inline AndroidBox androidPillarbox(uint32_t width,uint32_t height) {
    uint32_t boxWidth=std::min(width,height*4u/3u),boxHeight=std::min(height,width*3u/4u);
    return {int32_t((width-boxWidth)/2),int32_t((height-boxHeight)/2),boxWidth,boxHeight};
}
