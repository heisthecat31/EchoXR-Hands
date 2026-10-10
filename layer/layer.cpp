// EchoXR Hands' OpenXR API layer: your fingers from the game's own OpenXR session to the
// hand tracking plugin.
//
// Loaded by the Khronos loader into Echo VR (through EchoXR's LibOVR runtime) when the
// launcher enables it (XR_API_LAYER_PATH + XR_ENABLE_API_LAYERS). It adds
// XR_EXT_hand_tracking to the instance when the runtime offers it, tracks both hands in the
// game's session, and after every xrWaitFrame sends the plugin the frame the finger bridge
// used to send (HtvFrame to 127.0.0.1:8768): five curls per hand, worked out as the bridge
// did (common/hand_curls.h), splay 0. Being inside the game's session it works on every
// runtime with hand tracking (SteamVR, WiVRn, Monado), where a second app gets no input.
//
// Also from the bridge: Ctrl+Alt+C recalibrates (CalibrateHotkey), and CalibrateOnLaunch
// calibrates 10 s after the session first gets focus. Settings come from the plugin's
// EchoXRHands.txt; the layer logs to EchoXR\Hands\layer.log.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

#include "../common/hand_curls.h"
#include "../plugin/htv_protocol.h"

#define LAYER_NAME "XR_APILAYER_ECHOTOOLS_echoxr_hands"

// ---------------------------------------------------------------------------
// log and settings
// ---------------------------------------------------------------------------
static std::mutex g_LogLock;
static FILE* g_Log = nullptr;

static std::string ModuleDir(HMODULE m) {
    char p[MAX_PATH];
    DWORD n = GetModuleFileNameA(m, p, MAX_PATH);
    std::string s(p, n);
    size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? "" : s.substr(0, slash + 1);
}

static HMODULE ThisModule() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ThisModule, &m);
    return m;
}

static void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_LogLock);
    if (!g_Log) {
        // EchoXR\Hands\layer\ -> EchoXR\Hands\layer.log, new each start.
        std::string path = ModuleDir(ThisModule()) + "..\\layer.log";
        if (fopen_s(&g_Log, path.c_str(), "w") || !g_Log) return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_Log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_Log, fmt, a);
    va_end(a);
    fputc('\n', g_Log);
    fflush(g_Log);
}

struct Settings {
    bool hotkey = true;
    bool calibrateOnLaunch = false;
};

static std::string Trim(std::string s) {
    while (!s.empty() && strchr(" \t\r\n", s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// The plugin's settings beside the game: <bin\win10>\plugins\EchoXRHands.txt (the last value
// of a key wins, as in the plugin).
static Settings ReadSettings() {
    Settings s;
    std::string path = ModuleDir(nullptr) + "plugins\\EchoXRHands.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") || !f) {
        Log("no %s: default settings", path.c_str());
        return s;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == ';') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string k = Trim(line), v = Trim(eq + 1);
        size_t hash = v.find_first_of("#;");
        if (hash != std::string::npos) v = Trim(v.substr(0, hash));
        if (k == "CalibrateHotkey") s.hotkey = v != "0";
        else if (k == "CalibrateOnLaunch") s.calibrateOnLaunch = atoi(v.c_str()) != 0;
        else if (k == "FingerSource" && v == "device")
            Log("FingerSource = device: OpenXR has no finger sensing summary, the hand joints are used");
    }
    fclose(f);
    Log("settings: %s (hotkey %d, calibrate on launch %d)", path.c_str(), s.hotkey, s.calibrateOnLaunch);
    return s;
}

// ---------------------------------------------------------------------------
// to the plugin
// ---------------------------------------------------------------------------
static SOCKET g_Sock = INVALID_SOCKET;
static sockaddr_in g_To = {};

static void OpenSocket() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return;
    g_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nonblocking = 1;
    ioctlsocket(g_Sock, FIONBIO, &nonblocking);
    g_To.sin_family = AF_INET;
    g_To.sin_port = htons(HTV_PORT);
    g_To.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static void Send(const void* data, int len) {
    if (g_Sock != INVALID_SOCKET) sendto(g_Sock, (const char*)data, len, 0, (const sockaddr*)&g_To, sizeof(g_To));
}

// ---------------------------------------------------------------------------
// the next layer's (or the runtime's) functions
// ---------------------------------------------------------------------------
static PFN_xrGetInstanceProcAddr Next_GetInstanceProcAddr;
static PFN_xrDestroyInstance Next_DestroyInstance;
static PFN_xrCreateSession Next_CreateSession;
static PFN_xrDestroySession Next_DestroySession;
static PFN_xrWaitFrame Next_WaitFrame;
static PFN_xrPollEvent Next_PollEvent;
static PFN_xrGetSystemProperties Next_GetSystemProperties;
static PFN_xrCreateReferenceSpace Next_CreateReferenceSpace;
static PFN_xrDestroySpace Next_DestroySpace;
static PFN_xrCreateHandTrackerEXT Next_CreateHandTracker;
static PFN_xrDestroyHandTrackerEXT Next_DestroyHandTracker;
static PFN_xrLocateHandJointsEXT Next_LocateHandJoints;

// What the layer tracks: one session at a time (EchoXR's short field-of-view probe comes
// first, then the game's own).
struct Tracking {
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrHandTrackerEXT hand[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    hands::CurlState curl[2];
    float last[2][5] = {};
    uint32_t seq = 0;
    ULONGLONG focusedAt = 0;   // GetTickCount64 when the session first got focus
    bool launchCalibrated = false;
    bool hotkeyDown = false;
};

static std::mutex g_Lock;
static bool g_HandTracking = false;     // XR_EXT_hand_tracking is enabled on the instance
static bool g_MotionRange = false;      // XR_EXT_hand_joints_motion_range too
static Settings g_Settings;
static Tracking g_T;

static void StopTracking(Tracking& t) {
    for (XrHandTrackerEXT& h : t.hand)
        if (h != XR_NULL_HANDLE) {
            Next_DestroyHandTracker(h);
            h = XR_NULL_HANDLE;
        }
    if (t.space != XR_NULL_HANDLE) Next_DestroySpace(t.space);
    t = Tracking();
}

// ---------------------------------------------------------------------------
// intercepted functions
// ---------------------------------------------------------------------------
static XRAPI_ATTR XrResult XRAPI_CALL Layer_CreateSession(XrInstance instance, const XrSessionCreateInfo* info,
                                                         XrSession* session) {
    XrResult r = Next_CreateSession(instance, info, session);
    if (XR_FAILED(r) || !g_HandTracking) return r;

    XrSystemHandTrackingPropertiesEXT ht = { XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
    XrSystemProperties props = { XR_TYPE_SYSTEM_PROPERTIES };
    props.next = &ht;
    if (XR_FAILED(Next_GetSystemProperties(instance, info->systemId, &props)) || !ht.supportsHandTracking) {
        Log("session %p: the system has no hand tracking", (void*)*session);
        return r;
    }
    std::lock_guard<std::mutex> lock(g_Lock);
    if (g_T.session != XR_NULL_HANDLE) StopTracking(g_T);
    Tracking t;
    t.session = *session;
    XrReferenceSpaceCreateInfo si = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    si.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    si.poseInReferenceSpace.orientation.w = 1;
    XrResult rs = Next_CreateReferenceSpace(*session, &si, &t.space);
    for (int side = 0; side < 2; ++side) {
        XrHandTrackerCreateInfoEXT hi = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
        hi.hand = side == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        hi.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        XrResult rh = XR_SUCCEEDED(rs) ? Next_CreateHandTracker(*session, &hi, &t.hand[side]) : rs;
        if (XR_FAILED(rh)) {
            // EchoXR's probe session has no graphics: hand trackers may not come with it.
            Log("session %p: no %s hand tracker (%d)", (void*)*session, side ? "right" : "left", (int)rh);
            StopTracking(t);
            return r;
        }
    }
    g_T = t;
    g_Settings = ReadSettings();
    Log("session %p: tracking both hands%s", (void*)*session, g_MotionRange ? " (unobstructed motion range)" : "");
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_DestroySession(XrSession session) {
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (g_T.session == session) {
            StopTracking(g_T);
            Log("session %p ended", (void*)session);
        }
    }
    return Next_DestroySession(session);
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_PollEvent(XrInstance instance, XrEventDataBuffer* event) {
    XrResult r = Next_PollEvent(instance, event);
    if (r == XR_SUCCESS && event->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(event);
        std::lock_guard<std::mutex> lock(g_Lock);
        if (e->session == g_T.session && e->state == XR_SESSION_STATE_FOCUSED && !g_T.focusedAt)
            g_T.focusedAt = GetTickCount64();
    }
    return r;
}

// Both hands at the frame's display time, to the plugin.
static void SendHands(XrTime time) {
    HtvFrame frame = {};
    frame.magic = HTV_MAGIC;
    bool calibrate = false;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        Tracking& t = g_T;
        if (t.session == XR_NULL_HANDLE) return;
        frame.seq = ++t.seq;

        // Ctrl+Alt+C, and CalibrateOnLaunch 10 s after the session first had focus.
        bool down = g_Settings.hotkey && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                    (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState('C') & 0x8000);
        if (down && !t.hotkeyDown) calibrate = true;
        t.hotkeyDown = down;
        if (g_Settings.calibrateOnLaunch && t.focusedAt && !t.launchCalibrated &&
            GetTickCount64() - t.focusedAt >= 10000) {
            t.launchCalibrated = true;
            calibrate = true;
        }
        if (calibrate) t.curl[0].captureOpen = t.curl[1].captureOpen = true;

        for (int side = 0; side < 2; ++side) {
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
            XrHandJointLocationsEXT locs = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = joints;
            XrHandJointsMotionRangeInfoEXT range = { XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT };
            range.handJointsMotionRange = XR_HAND_JOINTS_MOTION_RANGE_UNOBSTRUCTED_EXT;
            XrHandJointsLocateInfoEXT li = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
            li.next = g_MotionRange ? &range : nullptr;
            li.baseSpace = t.space;
            li.time = time;
            if (XR_FAILED(Next_LocateHandJoints(t.hand[side], &li, &locs)) || !locs.isActive) continue;
            hands::Quat q[26];
            bool valid[26];
            for (int i = 0; i < 26; ++i) {
                const XrQuaternionf& o = joints[i].pose.orientation;
                q[i] = { o.x, o.y, o.z, o.w };
                valid[i] = (joints[i].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
            }
            if (!hands::Curls(q, valid, t.curl[side], t.last[side])) continue;
            frame.valid[side] = 1;
            memcpy(frame.curl[side], t.last[side], sizeof(frame.curl[side]));
        }
    }
    if (calibrate) {
        static const char text[] = "Calibrate = 1\n";
        Send(text, (int)sizeof(text) - 1);
        Log("calibrated: open hand taken as it is now");
    }
    Send(&frame, (int)sizeof(frame));
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_WaitFrame(XrSession session, const XrFrameWaitInfo* info,
                                                     XrFrameState* state) {
    XrResult r = Next_WaitFrame(session, info, state);
    if (XR_SUCCEEDED(r) && session == g_T.session) SendHands(state->predictedDisplayTime);
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_DestroyInstance(XrInstance instance) {
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (g_T.session != XR_NULL_HANDLE) StopTracking(g_T);
    }
    return Next_DestroyInstance(instance);
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_GetInstanceProcAddr(XrInstance instance, const char* name,
                                                               PFN_xrVoidFunction* fn) {
    struct Hook { const char* name; PFN_xrVoidFunction fn; };
    static const Hook hooks[] = {
        { "xrGetInstanceProcAddr", (PFN_xrVoidFunction)Layer_GetInstanceProcAddr },
        { "xrCreateSession", (PFN_xrVoidFunction)Layer_CreateSession },
        { "xrDestroySession", (PFN_xrVoidFunction)Layer_DestroySession },
        { "xrWaitFrame", (PFN_xrVoidFunction)Layer_WaitFrame },
        { "xrPollEvent", (PFN_xrVoidFunction)Layer_PollEvent },
        { "xrDestroyInstance", (PFN_xrVoidFunction)Layer_DestroyInstance },
    };
    for (const Hook& h : hooks)
        if (!strcmp(name, h.name) && instance != XR_NULL_HANDLE) {
            *fn = h.fn;
            return XR_SUCCESS;
        }
    if (!Next_GetInstanceProcAddr) return XR_ERROR_HANDLE_INVALID;
    return Next_GetInstanceProcAddr(instance, name, fn);
}

// Whether the runtime (below this layer) offers `ext`.
static bool Offered(const XrApiLayerCreateInfo* layerInfo, const char* ext) {
    PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
    layerInfo->nextInfo->nextGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
                                                 (PFN_xrVoidFunction*)&enumerate);
    uint32_t n = 0;
    if (!enumerate || XR_FAILED(enumerate(nullptr, 0, &n, nullptr))) return false;
    std::vector<XrExtensionProperties> props(n, { XR_TYPE_EXTENSION_PROPERTIES });
    if (XR_FAILED(enumerate(nullptr, n, &n, props.data()))) return false;
    for (const XrExtensionProperties& p : props)
        if (!strcmp(p.extensionName, ext)) return true;
    return false;
}

static XRAPI_ATTR XrResult XRAPI_CALL Layer_CreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                                                  const XrApiLayerCreateInfo* layerInfo,
                                                                  XrInstance* instance) {
    Next_GetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    // Hand tracking (and the controller-free motion range) when the runtime has them.
    std::vector<const char*> exts(info->enabledExtensionNames, info->enabledExtensionNames + info->enabledExtensionCount);
    auto add = [&](const char* ext) {
        for (const char* e : exts)
            if (!strcmp(e, ext)) return true;
        if (!Offered(layerInfo, ext)) return false;
        exts.push_back(ext);
        return true;
    };
    bool hand = add(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    bool range = hand && add(XR_EXT_HAND_JOINTS_MOTION_RANGE_EXTENSION_NAME);

    XrInstanceCreateInfo ours = *info;
    ours.enabledExtensionCount = (uint32_t)exts.size();
    ours.enabledExtensionNames = exts.data();
    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;
    XrResult r = layerInfo->nextInfo->nextCreateApiLayerInstance(&ours, &next, instance);
    if (XR_FAILED(r) && (hand || range) && exts.size() != info->enabledExtensionCount) {
        Log("the runtime refused hand tracking (%d): the game runs without it", (int)r);
        hand = range = false;
        r = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    }
    if (XR_FAILED(r)) return r;

    g_HandTracking = hand;
    g_MotionRange = range;
    auto load = [&](const char* name, auto& fn) {
        Next_GetInstanceProcAddr(*instance, name, (PFN_xrVoidFunction*)&fn);
    };
    load("xrDestroyInstance", Next_DestroyInstance);
    load("xrCreateSession", Next_CreateSession);
    load("xrDestroySession", Next_DestroySession);
    load("xrWaitFrame", Next_WaitFrame);
    load("xrPollEvent", Next_PollEvent);
    load("xrGetSystemProperties", Next_GetSystemProperties);
    load("xrCreateReferenceSpace", Next_CreateReferenceSpace);
    load("xrDestroySpace", Next_DestroySpace);
    if (hand) {
        load("xrCreateHandTrackerEXT", Next_CreateHandTracker);
        load("xrDestroyHandTrackerEXT", Next_DestroyHandTracker);
        load("xrLocateHandJointsEXT", Next_LocateHandJoints);
        g_HandTracking = Next_CreateHandTracker && Next_DestroyHandTracker && Next_LocateHandJoints;
    }
    if (g_Sock == INVALID_SOCKET) OpenSocket();
    Log("EchoXR Hands layer: instance %p, hand tracking %s", (void*)*instance,
        g_HandTracking ? (g_MotionRange ? "on (with motion range)" : "on") : "not offered by the runtime");
    return r;
}

extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request) {
    if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        (layerName && strcmp(layerName, LAYER_NAME)))
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = Layer_GetInstanceProcAddr;
    request->createApiLayerInstance = Layer_CreateApiLayerInstance;
    return XR_SUCCESS;
}
