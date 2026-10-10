# Credits

EchoXR Hands puts your own fingers on Echo VR's hands, from OpenXR hand tracking.

## People

- **heisthecat31** (he_is_the_cat): created EchoXR Hands. The finger plugin, how the
  fingers are posed and shared with other players, the settings window, the calibration,
  and the original SteamVR finger bridge whose curl maths the OpenXR layer keeps.

## What it builds on

| Project | By | Licence | How it is used |
| --- | --- | --- | --- |
| [nEVR runtime](https://github.com/EchoTools/nevr-runtime) | EchoTools | Apache-2.0 | The mod loader that loads the plugin |
| [EchoXR](https://github.com/EchoTools/EchoXR) | heisthecat31, marshmallow-mia | | Echo VR on OpenXR, which the layer runs in |
| [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) | The Khronos Group | Apache-2.0 | Headers for the API layer |
| [Microsoft Detours](https://github.com/microsoft/Detours) | Microsoft | MIT | `third_party/detours.h`, the plugin's hooks |

Echo VR is by Ready at Dawn and Meta, who have nothing to do with EchoXR Hands.
