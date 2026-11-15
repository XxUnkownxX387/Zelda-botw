// Frame generation (the "30 -> 60" option): the frame after a real frame, warped from it with the camera (and the
// depth) of the frame being generated - the port of LASRecomp's framegen (the LAS's reproj.cpp). Primitives: capture
// the camera's view-projection from the scene G-buffer's constant buffer, the player's position, and the generation
// itself; the backend swaps the presented frame (exec.c / device.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

typedef struct { float vp[4][4]; uint64_t size; bool valid; } FramegenCam;
extern FramegenCam g_framegen_cam;
void framegen_init(void);
void framegen_camera(const float *u, uint64_t size);  // (the scene G-buffer pass's UBO: the camera's view-projection)
void framegen_link(const float *p, uint64_t size);     // (the actor's position, per frame)
bool framegen_enabled(void);
bool framegen_generate(void);  // (the last real frame was composited: build the intermediate to present next)

// Integration with present path
void framegen_after_real_frame(VkCommandBuffer cb, VkImageView color_view, VkFormat color_fmt,
                                VkImageView depth_view, VkFormat depth_fmt, VkExtent2D extent);
bool framegen_should_generate(void);
void framegen_present_generated(VkCommandBuffer cb, VkImageView target_view, VkFormat target_fmt, VkExtent2D extent);
void framegen_get_current_vp(float out[4][4]);
