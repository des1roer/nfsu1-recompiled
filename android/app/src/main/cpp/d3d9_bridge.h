#pragma once
#include <cstdint>
struct ANativeWindow;
void configureD3D9Bridge(void* guestBase,uint32_t (*allocator)(uint32_t),void (*deallocator)(uint32_t));
void shutdownD3D9Bridge();
void setD3D9HostWindow(ANativeWindow* window);
void setD3D9RenderSize(unsigned width,unsigned height);
// "4:3 with bars": present the back buffer in a centred 4:3 box of the surface
// instead of filling it (the game's original geometry is 4:3).
void setD3D9Pillarbox(bool enabled);
// Launcher compatibility options; call before the game creates its device.
void setD3D9Compatibility(bool cpuTextures,unsigned backBuffers);
bool d3d9RequestsSurface();
void acknowledgeD3D9Surface(bool available);
uint32_t createGuestD3D9(uint32_t sdk);
uint32_t dispatchGuestD3D9(uint32_t token,const uint32_t* args,uint32_t* argumentCount);
// SetVertexShaderConstantF/SetPixelShaderConstantF calls and the ones skipped as duplicates
// since the last call; resets the counters.
void d3d9ShaderConstantStats(uint64_t* sets,uint64_t* hits);
