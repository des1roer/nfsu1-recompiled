/*
 * exe_layout.c - per-build guest addresses (see nfs_exe_layout.h).
 *
 * Speed.exe 1.4 (SHA-256 c0fad450...c074b7) is the documented build.
 *
 * The 2005 build (PE timestamp 2005-11-24, "Nov 18 2005" in its strings,
 * entry 0x270515, 3,183,036 bytes; found as a no-CD copy next to the
 * Universal Widescreen Patcher, whose speed.undo shows it unpatched) is a
 * separate compile: functions and globals sit at different, non-uniform
 * offsets and some calling conventions differ. Each address below was found
 * by its code, not by an offset:
 *   game_mouse    pushed for the mouse read 0x4131F0 (+4/+8 x/y, clamped)
 *   main_window   CreateWindowExA result at 0x408AC8, GetClientRect in 0x4132C0
 *   back_buffer   resolution setter 0x4087C0 (width from the stack, height esi)
 *   options       0x41E2D0 serializes 0x2E5 bytes of it into the .opt
 *   profile_*     0x41C880 tests the name, then writes the .opt (0x41E2D0)
 *   language_*    0x59F5D0: count-1 binary search over {key, value}, key edx
 *   resource_walk 0x447E60: data ecx, bytes eax, handler table on the stack
 *   mouse clamp   0x4132C0 = GetClientRect, then clamp [esi]/[edi]
 * The widescreen hook sites are 1.4's only, so that fix stays off here.
 */
#include <stddef.h>
#include "nfs_exe_layout.h"

static const nfs_exe_layout k_layouts[] = {
    { "Speed.exe 1.4", 0x270CB5u,
      0x00706498u, 0x00736380u, 0x0071A96Cu,
      0x007588C4u, 0x2E5u, 0x00736230u, 0x0041C7A0u,
      0x0059FB80u, 0x0073442Cu, 0x00734434u,
      0x00447C00u, 0,
      0x004131B5u, 1 },
    { "Speed.exe 2005 build", 0x270515u,
      0x007069F8u, 0x007368D8u, 0x0071AECCu,
      0x00758E14u, 0x2E5u, 0x00736788u, 0x0041C880u,
      0x0059F5D0u, 0x0073498Cu, 0x00734994u,
      0x00447E60u, 1,
      0x004132D5u, 0 },
};

const nfs_exe_layout *g_nfs_exe = &k_layouts[0];

const nfs_exe_layout *nfs_exe_select(uint32_t entry_rva) {
    for (size_t i = 0; i < sizeof k_layouts / sizeof k_layouts[0]; i++)
        if (k_layouts[i].entry_rva == entry_rva) return g_nfs_exe = &k_layouts[i];
    return NULL;
}
