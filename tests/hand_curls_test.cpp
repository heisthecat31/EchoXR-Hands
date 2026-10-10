// Tests for common/hand_curls.h. Plain C++, no Windows: from the repo root
//   c++ -std=c++17 -Icommon tests/hand_curls_test.cpp -o /tmp/hand_curls_test && /tmp/hand_curls_test
#include <stdio.h>
#include <stdlib.h>

#include "hand_curls.h"

using namespace hands;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool Near(float a, float b, float eps = 0.01f) { return fabsf(a - b) < eps; }

// A rotation of `deg` degrees about the X axis.
static Quat RotX(float deg) {
    float h = deg * 0.5f / 57.2957795f;
    return { sinf(h), 0, 0, cosf(h) };
}

// A hand whose joints all point the same way, except that each joint of `finger` is bent
// `perJoint` degrees further than its parent (joints in world orientation, as OpenXR gives
// them).
static void Hand(Quat joints[26], int finger, float perJoint) {
    for (int i = 0; i < 26; ++i) joints[i] = { 0, 0, 0, 1 };
    if (finger < 0) return;
    Quat q = { 0, 0, 0, 1 };
    for (int j = 0; j < 3; ++j) {
        q = Mul(q, RotX(perJoint));
        joints[kJoint0[finger] + j] = q;
    }
    joints[kJoint0[finger] + 3 < 26 ? kJoint0[finger] + 3 : 25] = q;
}

int main() {
    bool valid[26];
    for (bool& v : valid) v = true;
    Quat joints[26];
    float curl[5];

    // The first frame sets the open hand: all straight.
    CurlState st;
    Hand(joints, -1, 0);
    CHECK(Curls(joints, valid, st, curl));
    for (float c : curl) CHECK(Near(c, 0));

    // Index bent 70 degrees at each of its three joints: 210 = its whole span.
    Hand(joints, 1, 70);
    CHECK(Curls(joints, valid, st, curl));
    CHECK(Near(curl[1], 1));
    CHECK(Near(curl[0], 0) && Near(curl[2], 0) && Near(curl[3], 0) && Near(curl[4], 0));

    // Half of the middle finger's span (110 of 220).
    Hand(joints, 2, 110.0f / 3);
    CHECK(Curls(joints, valid, st, curl));
    CHECK(Near(curl[2], 0.5f));

    // The thumb: 40 of its 80.
    Hand(joints, 0, 40.0f / 3);
    CHECK(Curls(joints, valid, st, curl));
    CHECK(Near(curl[0], 0.5f));

    // A hand that starts bent: that's its zero until it opens further.
    CurlState bent;
    Hand(joints, 3, 20);
    CHECK(Curls(joints, valid, bent, curl));
    CHECK(Near(curl[3], 0));
    Hand(joints, -1, 0);
    CHECK(Curls(joints, valid, bent, curl));
    CHECK(Near(bent.open[3], 0));

    // Recalibration takes the hand as it is.
    Hand(joints, 4, 10);
    bent.captureOpen = true;
    CHECK(Curls(joints, valid, bent, curl));
    CHECK(Near(curl[4], 0) && Near(bent.open[4], 30, 0.1f));

    // A joint missing: no curls.
    valid[9] = false;
    CHECK(!Curls(joints, valid, st, curl));

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
