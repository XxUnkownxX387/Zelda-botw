// Frame generation (the "30 -> 60" option): the frame after a real frame, warped from it with the camera (and the
// depth) of the frame being generated - the port of LASRecomp's framegen (the LAS's reproj.cpp). Primitives: capture
// the camera's view-projection from the scene G-buffer's constant buffer, the player's position, and the generation
// itself; the backend swaps the presented frame (exec.c / device.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { float vp[4][4]; uint64_t size; bool valid; } FramegenCam;
extern FramegenCam g_framegen_cam;
void framegen_init(void);
void framegen_camera(const float *u, uint64_t size);  // (the scene G-buffer pass's UBO: the camera's view-projection)
void framegen_link(const float *p, uint64_t size);     // (the actor's position, per frame)
bool framegen_enabled(void);
bool framegen_generate(void);  // (the last real frame was composited: build the intermediate to present next)
