// Frame generation (the "30 -> 60" option): the frame after a real frame, warped from it with the camera's half step
// ahead - the port of LASRecomp's framegen (the LAS's reproj.cpp). Primitives: capture the camera's view-projection
// from the scene G-buffer's constant buffer, the player's position, and the generation itself; the backend swaps the
// presented frame (exec.c / device.c).
//
// The option (Video > Frame generation, runtime/settings.h g_settings.framegen, off by default): when it is on and the
// frame rate is below 60, the frames the player sees are generated - the real frames keep the rate they have (the game
// clock, hle/framepace.c) and the presents run at the doubled rate (nvnQueuePresentTexture) - so the player sees double.
#include "framegen.h"
#include "../../settings.h"
#include "../../framepace.h"
#include "../rt.h"

extern bool framepace_in_event(void);  // (hle/framepace.c: the cutscenes, the talks, the item fanfares)
extern volatile int g_movie_playing;  // (runtime/hle/movie.c: video decoders running)

FramegenCam g_framegen_cam;

void framegen_init(void) { g_framegen_cam.valid = false; }

void framegen_camera(const float *u, uint64_t size) {
    if (!u) return;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) g_framegen_cam.vp[r][c] = u[r * 4 + c];
    g_framegen_cam.size = size;
    g_framegen_cam.valid = true;
}

void framegen_link(const float *p, uint64_t size) { (void)p; (void)size; }  // (the player's position: not yet read)

bool framegen_enabled(void) { return g_settings.framegen; }  // (Video > Frame generation: off by default)

bool framegen_generate(void) {
    // (the frame after a real frame, generated from it - the intermediate to present: the warp of the scene with the
    // camera's half step ahead, the depth of the frame before, the HUD copied at the compositing - lands with the
    // backend; this is the generation's switch: a generated frame is owed when the option is on and the player is
    // actually moving in the world, and not in a menu, a movie, an event or a pause)
    if (!framegen_enabled()) return false;
    if (g_movie_playing > 0) return false;  // (a movie: the generated frames aren't its)
    if (framepace_in_menu()) return false;
    if (framepace_in_event()) return false;
    if (framepace_on_title()) return false;
    if (!framepace_on()) return false;
    // (the frames the player sees are the generated ones only when the frame generation doubled the rate: the real
    // frames keep the rate they have - framepace_real_period for the clock, framepace_period_ns for the presents - and
    // the frames between two real ones are generated. A rate the option didn't double (60 and above, the real frames')
    // aren't generated.)
    if (!framepace_gen_doubled()) return false;
    LOG("[framegen] a frame is generated between two real ones (the option is on, the world is running)\n");
    return true;
}
