<img src="logo/echoxr.png" width="96" alt="EchoXR logo">

# EchoXR Hands

EchoXR Hands puts **your own fingers on Echo VR's chassis hands**: every finger bends on
its own, and other players running it see them too.

It runs with [EchoXR](https://github.com/EchoTools/EchoXR) (Echo VR on OpenXR) and the
[nEVR runtime](https://github.com/EchoTools/nevr-runtime) mod loader. Your fingers come
from **OpenXR hand tracking in Echo VR's own session**, so it works on every OpenXR runtime
that has it: SteamVR, WiVRn, Monado. The [Echo VR launcher](https://github.com/marshmallow-mia/EchoVR_Launcher)
installs it (Mods → More mods → EchoXR Hands) and turns it on for each start.

Made by **heisthecat31**.

## What it does

| | |
| --- | --- |
| **Your own fingers** | Every finger bends on its own. Echo normally moves middle, ring and pinky together from the grip button. |
| **Any OpenXR runtime** | The layer asks the runtime for `XR_EXT_hand_tracking` in the game's session: Index or Touch controllers on SteamVR, the Quest's hand tracking on WiVRn, Monado's hand tracking. |
| **Other players' fingers** | The plugin sends your finger curls through a relay (`Network = 1`), and poses players who send theirs. |
| **Calibrate hotkey** | Ctrl+Alt+C recalibrates the open-hand pose at any time. |
| **Settings window** | `EchoXRSettings.exe` edits every setting, and most changes show in-game straight away. |

### Screenshots

<img src="docs/screenshots/settings-general.png" width="480" alt="EchoXR Hands settings, General page">

<img src="docs/screenshots/settings-pose.png" width="480" alt="EchoXR Hands settings, Pose page: a slider for each joint">

### Limits

- **It changes how hands look, not what they do.** Grabbing still uses the game's own
  grip input.
- **Other players see your tracked fingers only if they run the plugin.**
- **One game build.** The plugin's hooks are for the current `echovr.exe` (35,397,120
  bytes, May 2023). It checks the bytes it changes first, and refuses anything else.
- **Your runtime needs hand tracking.** Without `XR_EXT_hand_tracking` the layer logs so
  and the game plays with its own finger animation; you still see other players' fingers.
- **Finger spread** isn't sent (`SplayDeg` has nothing to work with yet).

## How it's put together

```
bin\win10\
  plugins\EchoXRHands.dll            the nEVR plugin: poses the hands, shares fingers
  plugins\EchoXRHands.txt            its settings (re-read while the game runs)
  EchoXR\Hands\
    layer\XrApiLayer_EchoXR_Hands.dll   the OpenXR API layer: reads your fingers
    layer\XrApiLayer_EchoXR_Hands.json  its manifest for the OpenXR loader
    EchoXRSettings.exe                 the settings window
```

- **The layer** loads into Echo VR's OpenXR instance (the launcher sets
  `XR_API_LAYER_PATH` to `EchoXR\Hands\layer` and
  `XR_ENABLE_API_LAYERS=XR_APILAYER_ECHOTOOLS_echoxr_hands`). It adds
  `XR_EXT_hand_tracking` (and `XR_EXT_hand_joints_motion_range`) to the instance when the
  runtime offers them, tracks both hands in the game's session, and after every frame
  sends the plugin five curls per hand over `127.0.0.1:8768` (`plugin/htv_protocol.h`).
  The curls are worked out in `common/hand_curls.h`: per finger, the bend of three joints
  against their parents, with the straightest pose this session as zero.
- **Why inside the game's session:** an OpenXR runtime gives input only to the session
  with focus. A separate app (the old SteamVR finger bridge) got fingers on SteamVR only.
- **The plugin** is loaded by nEVR from `plugins\` (it exports nEVR's plugin ABI,
  `plugin/nevr_plugin_interface.h`).

## Using it

1. Turn it on in the launcher (Mods → Additional Plugins → EchoXR Hands → Get) and play.
2. In-game, hold both hands **fully open** once: that captures the open-hand pose. To
   redo it at any time, hold your hands open and press **Ctrl+Alt+C**, or set
   `CalibrateOnLaunch = 1` (calibrates 10 s after Echo VR is first in your headset).
3. Settings: `EchoXR\Hands\EchoXRSettings.exe`, or `plugins\EchoXRHands.txt` by hand.

### Finger sharing and privacy

With `Network = 1`, the plugin connects to `RelayUrl`
(`wss://sparkapi-production-e6df.up.railway.app/htv/ws`). It sends your finger curls
(`NetSendHz` times a second), your display name, and the names of the players in your
match, so the relay can pair you with other players running the plugin. The relay
doesn't store anything. `Network = 0` keeps your tracking to yourself. The launcher
installs it with `Network = 0`.

## First-run tuning

| symptom | fix |
| --- | --- |
| fingers don't move at all | check `plugins\EchoXRHands.log` for `hooked` and `receiving tracking frames`; then `TargetInstance` |
| someone else's hands move | the log lists instances; set `TargetInstance` to yours |
| fingers bend backwards | flip `LeftBendSign` / `RightBendSign` |
| fingers bend sideways or twist | set `BendAxis` to `x`, `y` or `z` (default `auto`) |
| left and right swapped | `GameHandLeft = 1` |
| wrong finger moves | set `FingerOrder` explicitly, e.g. `index,middle,ring,pinky,thumb` |
| thumb wrong | `ThumbBendAxis`, `LeftThumbSign` / `RightThumbSign` |
| shaky / laggy | lower `FilterMinCutoff` / raise `FilterBeta` |

## How the hand tracking works

Echo runs its frame as a table of gamespace tasks. Two belong to
`CR15HandAnimatorCS`, back to back:

| phase | task | address |
| --- | --- | --- |
| `0x6008` | `UpdateFingerAnimPoses` | `echovr+0x99f440` |
| `0x6009` | `UpdateThumbPistonAnimPoses` | `echovr+0x9a16e0`, **hooked** |

`UpdateFingerAnimPoses` only queues jobs (`echovr+0x99f830`, one per finger) for a
hand whose **contact** weight is non-zero. Those jobs wrap the fingers around
whatever the hand is touching. A hand touching nothing gets no jobs, so at
`0x6009` its animation is already written and nothing else is still working on it.
The hook poses those hands and then calls the original. Hands in contact are left
to the engine (`RespectContact = 1`).

Joints are read and written with the engine's own accessors:

- `get` `echovr+0x331bd0` returns `{quat xyzw, pos, scale}`.
- `set` `echovr+0x37c8c0` writes a joint **and carries its children along**.

Each finger is set proximal → middle → distal. Every joint gets its rest rotation
from the chassis rig (`UseRig = 1`), with a bend of `curl × MaxCurlN` degrees. The
game's grip animation is therefore **replaced**, not stacked on top.

Other players' fingers come in through the relay. They're applied in
`CR15RemotePlayerCS::UpdateCachePoseForPhysics` (`echovr+0xd77be0`, phase
`0x600c`), and `CR15NetGame::Update` finds who is in the match.

Every hooked function's first bytes are checked before hooking, and any mismatch
leaves the game untouched. A fault while posing turns the plugin off for the
session rather than crashing the game.

## Logs

| log | what's in it |
| --- | --- |
| `plugins\EchoXRHands.log` | hooks, calibrations, finger order, hand-animator instances, relay status every 5 s, `receiving tracking frames from the hand tracking layer` |
| `EchoXR\Hands\layer.log` | the layer: whether the runtime offers hand tracking, the sessions it tracks, calibrations |

## Building

With CMake, MSVC on Windows or clang-cl + xwin from macOS/Linux:

```
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake   # cross
cmake --build build
python3 tools/make_release.py --no-build     # out/release/EchoXR-Hands-v<VERSION>-nevr.zip
c++ -std=c++17 -Icommon tests/hand_curls_test.cpp -o /tmp/t && /tmp/t   # the curl math's tests
```

Detours (MIT) and the OpenXR headers (Khronos OpenXR SDK 1.1.63, Apache-2.0) are fetched
by hash. The version is in `VERSION` (and the plugin's `NvrPluginGetInfo`).

| folder | what |
| --- | --- |
| `plugin/` | the nEVR plugin (hooks, posing, relay, Platform setting) |
| `layer/` | the OpenXR API layer and its manifest |
| `common/` | `hand_curls.h` (the curl math) and `ui.h` (the settings window's UI kit) |
| `settings/` | settings window (edits `EchoXRHands.txt` live) |
| `tests/` | tests for the curl math |
| `tools/` | release packaging, rig table generator, `fake_index.py` (test input without hand tracking) |
