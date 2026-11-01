# Embedding the game data in the APK, and supporting a patched Speed.exe

## Goal

Ship a personal Android build that already contains the PC game data (no manual
copy to `/storage/emulated/0/nfsu1` by the player), built from a user-owned copy
of the game that had the **Universal Widescreen Patcher** (`uniws.exe`) applied
to `Speed.exe`. Output: `build/NFSU1-embedded.apk`.

## 1. Bundling game data into the APK

The guest filesystem shim (`android/app/src/main/cpp/guest_files.inc`) does real
POSIX I/O (open/read/write/mkdir/stat) against `kGameRoot`
(`/storage/emulated/0/nfsu1`, see `game_data.h`). `AAssetManager` can't satisfy
that (read-only, no POSIX fd), and the data is ~1.3 GB, too big to read
piecemeal at runtime anyway. So "embedding" means: ship the data as assets,
then **extract it once to the same path the engine already expects**.

- Game folder copied into `android/app/src/main/assets/nfsu1/`, excluding
  PC-installer/patcher files the guest never reads (`AutoRun/`, `dimap.dll`,
  `uniws.exe`, `patches.ini`, `speed.undo`, `NFSU_icon.ico`, `filelist.txt`,
  `00000000.256`). This path is gitignored (`.gitignore`:
  `android/app/src/main/assets/`) — never commit it.
- `android/app/build.gradle`: `androidResources { noCompress "" }` stores every
  asset uncompressed. Needed both so `AssetManager.openFd()` works (only valid
  on uncompressed assets, used for sizing/progress) and to avoid recompressing
  already-compressed game data at build time.
- `android/app/src/main/java/com/nfsu1/androidevolved/GameDataInstaller.java`
  (new): recursively copies `assets/nfsu1/**` to `gameRoot()`, skipping if a
  `.installed` marker already matches the app's `versionCode`.
- Wired into `LauncherActivity.maybeInstallGameData()`, called from
  `onResume()` and `onRequestPermissionsResult()` instead of calling
  `refresh()` directly. Shows "Instalando archivos del juego… N%" on the
  existing `status` TextView while installing, via the existing `worker`
  executor.
- No manifest changes: `MANAGE_EXTERNAL_STORAGE` was already requested and is
  still needed to write to `/storage/emulated/0/nfsu1`.

To redo this for a different game-data folder: delete
`android/app/src/main/assets/nfsu1/`, re-copy the new folder there (same
exclude list), bump `versionCode` if you want existing installs to
re-extract, rebuild.

## 2. A one-time prerequisite this project doesn't document up front

`android/app/src/main/cpp/CMakeLists.txt` requires `src/recomp/gen/recomp_*.c`
to already exist (`FATAL_ERROR "Generate the local game C first"`) and
`src/recomp/gen_abk/abk_*.c` too (sound banks, also `FATAL_ERROR` if absent).
Neither is committed (gitignored, "everything derived from the game binary
stays local"). Before any Android build:

```bash
mkdir -p work
cp "<game folder>/Speed.exe" work/Speed.analysis.exe
source scripts/vsenv.sh x86
python scripts/data_seeds.py work/Speed.analysis.exe work/seeds_all.json
python scripts/run_lift.py catalog work/Speed.analysis.exe work/seeds_all.json work/catalog.json
python scripts/run_lift.py lift work/Speed.analysis.exe work/catalog.json src/recomp/gen
python scripts/abk_module.py "<game folder>" work/abk_analysis.dll work/abk_seeds.json src/recomp/gen_abk/abk_blocks.c
python scripts/run_lift.py catalog work/abk_analysis.dll work/abk_seeds.json work/abk_catalog.json
python scripts/run_lift.py lift work/abk_analysis.dll work/abk_catalog.json src/recomp/gen_abk --prefix abk_
```

Timing on this machine: catalog ~5 min (20193 functions discovered), main lift
~90s, ABK lift ~instant (28 functions). Needs Visual Studio MSVC x86
(`scripts/vsenv.sh x86`), Python with `capstone`+`pefile`, and the sibling
`../pcrecomp` checkout (already present at `C:\dev\pcrecomp`). `src/recomp/gen`
and `gen_abk` must exist (mkdir them) before the first `run_lift.py lift` call.

Local Android SDK used: `C:\Android\Sdk` (NDK 28.2.13676358, CMake 3.30.5,
platform android-35, all matching `android/app/build.gradle` already —
licenses pre-accepted). Point Gradle at it via `android/local.properties`
(`sdk.dir=C\:\\Android\\Sdk`, gitignored). Build with
`android/gradlew.bat assembleDebug` (no release signing config exists in this
project, debug is fine for sideloading).

## 3. The mechanism: accepting a patched Speed.exe

### The problem

Every address in this project (hook sites, overrides, the ABK call table) is
pinned to one documented binary: Speed.exe 1.4, SHA-256
`c0fad450…c074b7` (image base `0x400000`, size of image `0x394368`, entry
point `0x270CB5`, file size 3,178,496). A third-party patcher that changes
even a few bytes can shift everything after the change point. NFSU2's
`memory.md` (https://github.com/des1roer/nfsu2-recompiled/blob/main/memory.md)
documents the general pattern for this (`NFSU2_EXE_BUILD`, per-build
`config/hooks-<tag>.txt`, `#if defined(NFSU2_EXE_<TAG>)` in `widescreen.c`).
NFSU1 did **not** have this per-build infrastructure yet; this session dealt
with one specific build ad hoc instead of building the full generalized
mechanism (see "What wasn't done" below for why, and what porting the
real mechanism here would take).

The exe actually used: same `Speed.exe` patched by the **Universal Widescreen
Patcher** (`uniws.exe`, identified by `patches.ini`/`speed.undo` alongside
it). Measured: file size 3,183,036; image base unchanged `0x400000`; size of
image `0x39B5BC`; entry point `0x270515` (1952 bytes earlier than the
original — the patcher's init stub runs before jumping to the real entry).

### What was fixed, and how each address was verified (not guessed)

1. **PE image validation** (`android/app/src/main/cpp/game_data.h`,
   `guest_runtime.cpp`'s `loadImage()`): checks `imageBase` and
   `AddressOfEntryPoint` against an allow-list of **two** known entry points
   (`0x270CB5` original, `0x270515` uniws-patched) instead of one. `imageSize`
   is no longer compared at all — same fix as NFSU2's `loadImage()` ("No-CD
   builds can report an unaligned SizeOfImage; map every section in full"):
   it's recomputed from the actual section table
   (`max(VirtualAddress + max(VirtualSize, SizeOfRawData))`, page-rounded)
   instead of trusting the PE header's declared value.

2. **`config/overrides.txt` / `src/runtime/overrides.c`** (functions where
   normal auto-lift is known-wrong): of the 3 documented overrides, only the
   Cyrix probe's address needed fixing. Found by an **exact 19-byte signature
   search** (`33 C0 9E B8 05 00 00 00 BB 02 00 00 00 F6 F3 9F 80 FC 02` =
   `xor eax,eax; sahf; mov eax,5; mov ebx,2; div bl; lahf; cmp ah,2`, the
   literal sequence documented in `overrides.c`'s comment) over the patched
   exe's raw bytes — single unique hit at `0x006432AA`; the function itself
   starts one byte earlier at `0x006432A9` (`push ebx`, confirmed by checking
   that a `ret` immediately precedes it). Added as an **additional** entry in
   both files (not a replacement) — the original address still matches a
   genuine, unpatched build, so both builds work from the same tables. The
   two CRT `memcpy`/`memmove` overrides (`0x00672630`, `0x00678D30`) were
   **left unfixed**: no reliable byte signature was available (no original
   exe to diff against, and the structural description alone isn't safely
   searchable), and the generated code has zero `/* unresolved */` markers
   (`grep` for pcrecomp's exact unresolved-ICALL/ITAIL comment text) — the
   literal symptom the override exists to prevent is absent in this build,
   so leaving the normal auto-lift in place is likely correct, and a wrong
   guess here would silently replace some *other*, correctly-lifted
   function.

3. **`scripts/abk_module.py`** (sound-bank code generation, required by
   `android/app/src/main/cpp/CMakeLists.txt`'s `FATAL_ERROR` if
   `gen_abk/abk_*.c` is missing): same image-base/entry-point relaxation as
   #1. Its `CALL_TABLE = 0x6E9AE8` (Speed.exe's AEMS function-pointer table)
   was checked **directly**, not assumed: dumped 24 consecutive dwords at
   that address in the patched exe and confirmed all 24 fall in the valid
   code range `[0x401000, 0x697000)` with a clean incrementing pattern —
   the table did **not** move. Ran `abk_module.py` against it: 23 distinct
   code blocks (matches the documented "106 banks hold only 23 distinct
   blocks" exactly), 0 range-check failures on any resolved call target.

   A `+0x2E8` shift was found for two *data* globals (`fHudScaleX`/`fHudPosX`
   anchor, originally `0x6CC914`/`0x6CC910`) via `scripts/widescreen_sites.py`
   pattern search — **do not** assume this delta applies anywhere else. It
   turned out not to apply to `CALL_TABLE` at all (verified above). The most
   likely explanation: uniws.exe rewrites a handful of *specific*
   instructions in place (including these two to point at its own new
   globals) rather than uniformly relocating the whole data section — each
   address needs independent verification, not delta extrapolation.

### What wasn't done (and why)

- **`config/hooks.txt`** (~55 widescreen/touch hook sites,
  `src/runtime/widescreen.c`'s `recomp_hook()`) was **not** ported to this
  build. Running `scripts/widescreen_sites.py` against the patched exe found:
  the `mirror` pattern (rear-view mirror quad) — 0 hits; the three `neg 320`
  patterns, which should cover ~30-40 "mouse hit test" sites per
  `hooks.txt` — only 5 hits total. Most likely cause: the widescreen patcher
  targets exactly this kind of hardcoded-320 code, so ThirteenAG's byte
  signatures (tuned for the unpatched 1.4 build) don't match what's there
  now. Properly supporting this would need NFSU2's full mechanism (a
  `config/hooks-<tag>.txt` + `#if defined(NFSU1_EXE_<TAG>)` table in
  `widescreen.c`, picked by an env var read at both lift time in
  `run_lift.py` and compile time in both `CMakeLists.txt` files — see the
  referenced NFSU2 `memory.md` for the exact shape), and each of the ~55
  sites would need the same kind of careful, individually-verified relocation
  as the Cyrix probe above — not finished.
- **Mitigation in place**: `recomp_hook()` has
  `if (s_display_aspect <= 0.0f) return;` **before** the `k_hit[]` loop and
  the `switch`. As long as the launcher's widescreen option is left off,
  `s_display_aspect` stays 0 and every hook call for an unmapped/wrong
  address returns immediately — it never reaches the `switch`'s `default:`
  fatal-crash branch. **Do not enable native widescreen in the launcher on
  this build** until `hooks.txt` is properly redone for this exe.
- The initial `unimplemented_total: 3075` (`2990` of them `3dnow`) in the
  main lift's stats was investigated and is **not** believed to be a sign of
  a broken lift: the generated C around those sites
  (`src/recomp/gen/recomp_0032.c`) shows coherent, correctly-structured
  MMX/3DNow vector code (`movq`, `pxor` lifted normally, interleaved with
  `RECOMP_UNIMPL` only for the 3DNow-specific opcodes) — consistent with a
  CPU-dispatch codepath (AMD 3DNow optimization branch) that real hardware
  today never selects, so these `RECOMP_UNIMPL` traps are plausibly dead code
  in practice. No reference lift of the genuine unpatched 1.4 exists in this
  checkout to confirm the exact number is "normal," though.

### Build result

`android/gradlew.bat assembleDebug` succeeded (8m47s) after the above fixes
and the one-time lift pipeline run. Output copied to
`build/NFSU1-embedded.apk` (~1.42 GB). **Not tested on an actual device** —
only the exe-specific pieces above (Cyrix probe, ABK table) were verified by
direct inspection of bytes/addresses, not by running the game.

## Rule for anything going further

If you add proper multi-build support for `hooks.txt` later (porting NFSU2's
`NFSU2_EXE_BUILD` mechanism as `NFSU1_EXE_BUILD`), keep it additive: this
session's two extra entries (the image-check allow-list, the second Cyrix
override) never replaced the original build's values, only added alongside
them, so a genuine unpatched Speed.exe 1.4 still works unmodified. Follow the
same pattern for any future addition — never make the *documented* build's
support conditional on these changes.
