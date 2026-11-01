/*
 * nfs_exe_layout.h - the guest addresses the host runtime reads, writes or
 * calls directly, for each Speed.exe build it accepts.
 *
 * The lifted code itself comes from whatever exe was lifted; these are only
 * the host's own references into it (asset guards, touch cursor, window and
 * back buffer, options save, cursor-clamp hook). A build is identified by its
 * PE entry point. A field left 0 turns that host feature off for the build.
 */
#ifndef NFS_EXE_LAYOUT_H
#define NFS_EXE_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint32_t entry_rva;          /* AddressOfEntryPoint that identifies the build */
    uint32_t game_mouse;         /* DirectInput mouse object: +4 x, +8 y (window px) */
    uint32_t main_window;        /* HWND of the game window */
    uint32_t back_buffer;        /* D3DPRESENT_PARAMETERS: width, height at +4 */
    uint32_t options;            /* live options serialized into the profile .opt */
    uint32_t options_bytes;
    uint32_t profile_name;       /* loaded profile name (empty: none) */
    uint32_t profile_save;       /* profile config save (.opt, then .cfg), no args */
    uint32_t language_lookup;    /* binary search: key in edx, value in eax */
    uint32_t language_count;
    uint32_t language_table;     /* {key, value} pairs */
    uint32_t resource_walk;      /* resource chunk walker */
    int resource_walk_regs;      /* 1: data in ecx, bytes in eax; 0: stack args 2, 3 */
    uint32_t mouse_clamp_hook;   /* after GetClientRect in the cursor clamp; rect at [esp] */
    int widescreen;              /* config/hooks.txt widescreen sites fit this build */
} nfs_exe_layout;

/* Speed.exe 1.4 until the guest image is loaded. */
extern const nfs_exe_layout *g_nfs_exe;
/* The layout of the build with this entry point (also made current), or NULL. */
const nfs_exe_layout *nfs_exe_select(uint32_t entry_rva);

#ifdef __cplusplus
}
#endif
#endif
