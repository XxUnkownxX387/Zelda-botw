// Frame generation (the "30 -> 60" option): the frame after a real frame, warped from it with the camera (and the
// depth) of the frame being generated - the port of LASRecomp's framegen (the LAS's reproj.cpp). Primitives: capture
// the camera's view-projection from the scene G-buffer's constant buffer, the player's position, and the generation
// itself; the backend swaps the presented frame (exec.c / device.c).
#include "framegen.h"

FramegenCam g_framegen_cam;

void framegen_init(void) { g_framegen_cam.valid = false; }

void framegen_camera(const float *u, uint64_t size) {
    if (!u) return;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) g_framegen_cam.vp[r][c] = u[r * 4 + c];
    g_framegen_cam.size = size;
    g_framegen_cam.valid = true;
}

void framegen_link(const float *p, uint64_t size) { (void)p; (void)size; }  // (the player's position: not yet read)

bool framegen_enabled(void) { return false; }  // (the Video tab's option: off until the port is finished)

bool framegen_generate(void) { return false; }  // (the intermediate frame: not ported yet)
