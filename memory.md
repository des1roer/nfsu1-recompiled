# Embedding the game data in the APK, and supporting a patched Speed.exe

## Goal

Ship a personal Android build that already contains the PC game data (no manual
copy to `/storage/emulated/0/nfsu1` by the player), built from a user-owned copy
of the game whose `Speed.exe` is **not 1.4** but a 2005 build (section 3).
Output: `build/NFSU1-embedded.apk`.

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

## 3. Supporting a second Speed.exe build

### What the exe actually is (corrected)

The first version of this file called it "Speed.exe 1.4 patched by the
Universal Widescreen Patcher". That was wrong, and it is why the first APK
stopped at boot with `Arranque guest detenido: No guest function at
0x0059fb80`:

- PE timestamp 2005-11-24, `Nov 18 2005` in its strings, entry `0x270515`,
  3,183,036 bytes, SHA-256 `1b232d7a…f83ebbf6`. Speed.exe 1.4 is entry
  `0x270CB5`, 3,178,496 bytes, SHA-256 `c0fad450…c074b7`.
- `uniws.exe`'s only NFSU patch is the 640x480 immediates
  (`B8 80 02 00 00 BE E0 01 00 00`, `patches.ini`); this exe still has them at
  `0x4088A5`, and `speed.undo` lists 640/480 too. So the patch is not applied.
- It is a separate compile ("cracked SPEED.EXE" per uniws' notes): functions
  and globals move by different, non-uniform amounts (`-0x5B0`, `+0xE0`,
  `+0x110`, `+0x560`, `+0x558`...), and some calling conventions changed (the
  resource walker takes its buffer in `ecx`/`eax` here, on the stack in 1.4).
  **Never extrapolate an address by a delta.**

The lifted C is fine either way (it is generated from whatever exe is
lifted). What broke was the **host runtime's own references into the guest**:
1.4 constants for functions it calls, globals it reads/writes, and hook sites.

### The mechanism

- `src/runtime/nfs_exe_layout.h` + `exe_layout.c`: one table row per build
  (picked by entry point in `loadImage()`, `nfs_exe_select`), with every guest
  address the host uses: touch mouse object, game HWND, back buffer size,
  options/profile save, language table search, resource walker (+ its ABI),
  cursor-clamp hook site, and whether 1.4's widescreen hooks apply. A 0 field
  turns that host feature off for the build. `loadImage()` also refuses an exe
  whose entry is not a lifted function (lifted code from another exe).
- Users: `guest_asset_guards.inc`, `nfs_validate_asset_entry`,
  `saveChangedOptions`, `clientRect`/`pointerInWindow`, the touch mouse
  delta in `guest_input.inc`, `recomp_hook`/minimap in `widescreen.c`,
  `overrides.c` (each override is tagged with its build's entry point).
  `arm64_guest.h`'s `RECOMP_ENTER` lists both builds' guarded functions as
  constants, so it still folds away in every other function.
- `config/hooks.txt`, `config/overrides.txt`: `@1.4` / `@2005` after the VA.
  `scripts/run_lift.py` detects the build (`EXE_BUILDS`) and lifts only that
  build's entries; at another build's address they are unrelated code.

### 2005 build addresses (each found by its code, see `exe_layout.c`)

| what | 1.4 | 2005 | how it was identified |
|---|---|---|---|
| language search | `0x59FB80` | `0x59F5D0` | count-1 binary search over {key,value}, key in edx |
| its count/table | `0x73442C`/`0x734434` | `0x73498C`/`0x734994` | read by that search |
| resource walker | `0x447C00` (stack 2,3) | `0x447E60` (ecx, eax) | walks `+[x+4]+8`, handler table on the stack |
| profile save | `0x41C7A0` | `0x41C880` | tests profile name, calls the .opt writer |
| .opt writer / options | `0x41E200` / `0x7588C4` | `0x41E2D0` / `0x758E14` | serializes 0x2E5 bytes of it |
| profile name | `0x736230` | `0x736788` | byte tested by the profile save |
| game HWND | `0x736380` | `0x7368D8` | CreateWindowExA result, used by the clamp |
| back buffer | `0x71A96C` | `0x71AECC` | resolution setter `0x4087C0` |
| mouse object | `0x706498` | `0x7069F8` | pushed for the mouse read `0x4131F0` |
| cursor clamp hook | `0x4131B5` | `0x4132D5` | after GetClientRect in `0x4132C0` |
| CRT memmove | `0x672630`, `0x678D30` | `0x671E80`, `0x678580` | same bytes as SPEED2 `0x75CFD0` except jump tables |
| Cyrix probe | `0x643A39` | `0x6432A9` | 19-byte signature |
| minimap hooks | `0x590DDF`... | `0x59083F`, `0x58F57C`, `0x58E012` | same opcodes |

`scripts/abk_module.py`'s `CALL_TABLE = 0x6E9AE8` was checked directly and
did not move (24 code pointers, 23 distinct blocks, as in 1.4).

### Not supported on the 2005 build

- Native widescreen (render size, FOV, HUD, the 30 front-end hit tests): its
  ~55 sites are 1.4's only. The launcher option is ignored for this build
  (logged "Widescreen unavailable for ..."); the game stays 4:3.
- `src/runtime/nfs_runtime.c` (Windows desktop host only) still names 1.4's
  timer list in its crash report; the desktop host stays 1.4-only.

### Rebuilding after a change to hooks/overrides

Only the main lift needs re-running (`run_lift.py lift ...`, ~2 min); the
catalog does not depend on hooks/overrides. Then `android/gradlew.bat
assembleDebug` (run it as `./gradlew.bat` from `android/` in Git Bash).

## Rule for anything going further

Additive and verified: a new build gets its own row in `exe_layout.c` and
`@tag` entries in `config/`, every address found by its code (signature,
callers, the globals it touches), never by an offset from another build.
Speed.exe 1.4's values never change.
