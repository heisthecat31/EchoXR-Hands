// Finger curls from a hand's joint orientations, as the finger bridge worked them out from
// SteamVR's skeleton (bridge "bones" mode) -- now from OpenXR's XR_EXT_hand_tracking.
//
// OpenXR's hand joints 1-25 are numbered as OpenVR's hand bones 1-25 (wrist, then thumb
// metacarpal..tip, then index, middle, ring and little metacarpal..tip), so the same joints
// are read: per finger the three from kJoint0[f], each relative to the joint before it (its
// parent). A finger's raw bend is the sum of those three rotation angles; its zero is the
// straightest the finger has been this session (holding the hand open once sets it, and a
// recalibration takes the hand as it is), and a full fist is kSpanDeg further.
//
// Pure: no OpenXR or Windows types, so it is tested on its own (tests/hand_curls_test.cpp).
#pragma once
#include <math.h>
#include <string.h>

namespace hands {

struct Quat { float x, y, z, w; };

// The first of each finger's three joints (thumb, index, middle, ring, little).
static const int kJoint0[5] = { 3, 8, 13, 18, 23 };
// How far the three joints bend together from an open hand to a full fist, in degrees.
static const float kSpanDeg[5] = { 80.0f, 210.0f, 220.0f, 220.0f, 210.0f };

inline Quat Conj(const Quat& q) { return { -q.x, -q.y, -q.z, q.w }; }

inline Quat Mul(const Quat& a, const Quat& b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

// The rotation angle of `q` in degrees (0..180).
inline float AngleDeg(const Quat& q) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    float w = n > 0 ? fabsf(q.w) / n : 1.0f;
    if (w > 1) w = 1;
    return 2.0f * acosf(w) * 57.2957795f;
}

// A hand's open-hand zero, kept across frames.
struct CurlState {
    bool haveOpen = false;
    bool captureOpen = false;   // set to take the next frame as the open hand
    float open[5] = {};
};

// The five curls (0 = straight, 1 = fist) from 26 joint orientations in any one space;
// `valid[i]` says joint i's orientation is valid. False (curls untouched) when a joint
// needed is missing.
inline bool Curls(const Quat joints[26], const bool valid[26], CurlState& st, float curl[5]) {
    float raw[5];
    for (int f = 0; f < 5; ++f) {
        raw[f] = 0;
        for (int j = 0; j < 3; ++j) {
            int i = kJoint0[f] + j;
            if (!valid[i] || !valid[i - 1]) return false;
            raw[f] += AngleDeg(Mul(Conj(joints[i - 1]), joints[i]));
        }
    }
    if (!st.haveOpen || st.captureOpen) {
        memcpy(st.open, raw, sizeof(st.open));
        st.haveOpen = true;
        st.captureOpen = false;
    }
    for (int f = 0; f < 5; ++f) {
        if (raw[f] < st.open[f]) st.open[f] = raw[f];
        float c = (raw[f] - st.open[f]) / kSpanDeg[f];
        curl[f] = c < 0 ? 0 : c > 1 ? 1 : c;
    }
    return true;
}

}  // namespace hands
