"""Builds the EchoXR Hands release the Echo VR launcher installs:
out/release/EchoXR-Hands-v<VERSION>-nevr.zip (and its .sha256).

    python3 tools/make_release.py              build (CMake, build/), then package
    python3 tools/make_release.py --no-build   package what's already in out/

The zip unpacks into Echo VR's bin\\win10 folder. Everything is under EchoXR\\Hands\\; the
launcher copies the plugin and its settings into bin\\win10\\plugins\\ itself (nEVR loads
plugins from there), so there is deliberately no install\\ folder:

    EchoXR\\Hands\\plugin\\EchoXRHands.dll, EchoXRHands.txt   the nEVR plugin, default settings
    EchoXR\\Hands\\layer\\                                     the OpenXR API layer + manifest
    EchoXR\\Hands\\EchoXRSettings.exe                          the settings window
    EchoXR\\Hands\\README.txt, version.txt

Raise VERSION for every release (and the plugin's NvrPluginGetInfo version with it).
"""
import hashlib
import os
import subprocess
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
VERSION = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()

# (source relative to ROOT, path inside the zip)
FILES = [
    ("out/EchoXRHands.dll",                          "EchoXR/Hands/plugin/EchoXRHands.dll"),
    ("out/EchoXRHands.txt",                          "EchoXR/Hands/plugin/EchoXRHands.txt"),
    ("out/layer/XrApiLayer_EchoXR_Hands.dll",        "EchoXR/Hands/layer/XrApiLayer_EchoXR_Hands.dll"),
    ("out/layer/XrApiLayer_EchoXR_Hands.json",       "EchoXR/Hands/layer/XrApiLayer_EchoXR_Hands.json"),
    ("out/EchoXRSettings.exe",                       "EchoXR/Hands/EchoXRSettings.exe"),
]
FOLDERS = ("EchoXR/", "EchoXR/Hands/", "EchoXR/Hands/plugin/", "EchoXR/Hands/layer/")

README = """EchoXR Hands {version}
=================
Your own fingers on Echo VR's hands, and other players'. Made by heisthecat31:
https://github.com/EchoTools/EchoXR-Hands

Your fingers come from OpenXR hand tracking in Echo VR's own session (SteamVR, WiVRn,
Monado), through the layer in layer\\; the nEVR plugin (plugin\\EchoXRHands.dll, which
the Echo VR launcher puts into bin\\win10\\plugins\\) poses the hands and shares fingers.
The launcher installs and enables all of it: Mods, More mods, EchoXR Hands.

Hold both hands fully open once in-game to calibrate; Ctrl+Alt+C recalibrates.
Settings: EchoXRSettings.exe, or bin\\win10\\plugins\\EchoXRHands.txt.
Logs: bin\\win10\\plugins\\EchoXRHands.log and EchoXR\\Hands\\layer.log.

Finger sharing (Network = 1) sends your finger curls, your display name and your
match's player names to the hand tracking relay; the launcher installs it off.
"""


def build():
    subprocess.check_call(["cmake", "--build", os.path.join(ROOT, "build")], cwd=ROOT)


def package():
    out = os.path.join(ROOT, "out", "release")
    os.makedirs(out, exist_ok=True)
    made = os.path.join(out, "EchoXR-Hands-v%s-nevr.zip" % VERSION)
    with zipfile.ZipFile(made, "w", zipfile.ZIP_DEFLATED) as z:
        for folder in FOLDERS:
            info = zipfile.ZipInfo(folder, (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o40755 << 16
            z.writestr(info, "")
        for src, dst in FILES:
            path = os.path.join(ROOT, src)
            if not os.path.isfile(path):
                sys.exit("missing %s: build first" % src)
            z.write(path, dst)
        for name, text in (("EchoXR/Hands/README.txt", README.format(version="v" + VERSION)),
                           ("EchoXR/Hands/version.txt", "v%s\n" % VERSION)):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            z.writestr(info, text.replace("\n", "\r\n"))
    digest = hashlib.sha256(open(made, "rb").read()).hexdigest()
    with open(made + ".sha256", "w") as f:
        f.write("%s  %s\n" % (digest, os.path.basename(made)))
    print(made)
    print(digest)


if __name__ == "__main__":
    if "--no-build" not in sys.argv:
        build()
    package()
