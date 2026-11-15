// Frame generation (the "30 -> 60" option): the frame after a real frame, warped from it with the camera's half step
// ahead - the port of LASRecomp's framegen (the LAS's reproj.cpp). Primitives: capture the camera's view-projection
// from the scene G-buffer's constant buffer, the player's position, and the generation itself; the backend swaps the
// presented frame (exec.c / device.c).
//
// The option (Video > Frame generation, runtime/settings.h g_settings.framegen, off by default): when it is on and the
// frame rate is below 60, the frames the player sees are generated - the real frames keep the rate they have (the game
// clock, hle/framepace.c) and the presents run at the doubled rate (nvnQueuePresentTexture) - so the player sees double.
#include "framegen.h"
#include "vk.h"
#include "shaders.h"
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
    static int logged = -1;
    if (logged < 0) {
        LOG("[framegen] framegen_generate: enabled=%d, framepace_doubled=%d, movie=%d, menu=%d, event=%d, title=%d, pace_on=%d\n",
            framegen_enabled(), framepace_gen_doubled(), g_movie_playing > 0, framepace_in_menu(), framepace_in_event(), framepace_on_title(), framepace_on());
        logged = 1;
    }
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

// Called when a real frame is presented - store its color/depth for next frame's warp
void framegen_store_prev_frame(VkCommandBuffer cb, VkImage color_image, VkImageView color_view, VkFormat color_fmt,
                                VkImage depth_image, VkImageView depth_view, VkFormat depth_fmt,
                                VkExtent2D extent, const float vp[4][4]) {
    if (!framegen_enabled() || !framepace_gen_doubled()) return;

    LOG("[framegen] store_prev_frame: color_view=%p, depth_view=%p, fg_has_prev_frame_before=%d\n",
        color_view, depth_view, fg_has_prev_frame);

    // Store previous frame's VP for half-step calculation
    if (fg_has_prev_frame) {
        // Compute half-step VP: interpolate between prev and current
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                fg_gen_vp[r][c] = (fg_prev_vp[r][c] + vp[r][c]) * 0.5f;
            }
        }
    } else {
        // First frame - just copy
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                fg_gen_vp[r][c] = vp[r][c];
            }
        }
    }
    memcpy(fg_prev_vp, vp, sizeof fg_prev_vp);
    fg_has_prev_frame = true;

    LOG("[framegen] store_prev_frame: fg_has_prev_frame_after=%d\n", fg_has_prev_frame);

    // Store color/depth images for warp
    fg_prev_color_image = color_image;
    fg_prev_color_view = color_view;
    fg_prev_depth_image = depth_image;
    fg_prev_depth_view = depth_view;
    fg_color_format = color_fmt;
    fg_depth_format = depth_fmt;
    fg_extent = extent;
}

// Framegen capture is now done at end of main scene pass (vk_end_pass in exec.c)
// void framegen_after_real_frame(...) { ... }

// Check if we should generate a frame instead of presenting real frame
bool framegen_should_generate(void) {
    static int logged = -1;
    bool result = framegen_enabled() && framepace_gen_doubled() && fg_has_prev_frame;
    if (logged < 0) {
        LOG("[framegen] framegen_should_generate: enabled=%d, doubled=%d, has_prev=%d -> %d\n",
            framegen_enabled(), framepace_gen_doubled(), fg_has_prev_frame, result);
        logged = 1;
    }
    return result;
}

// Render a generated frame (warp) to the swapchain image
void framegen_present_generated(VkCommandBuffer cb, VkImageView target_view, VkFormat target_fmt, VkExtent2D extent) {
    if (!framegen_should_generate()) return;

    LOG("[framegen] a frame is generated between two real ones (the option is on, the world is running)\n");

    if (!fg_prev_color_view || !fg_prev_depth_view) {
        // Fallback: clear black if no previous frame data
        VkClearColorValue clear = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        extern NvnTexture* framegen_get_target_texture(void);
        NvnTexture* target_tex = framegen_get_target_texture();
        if (target_tex) {
            TexBackend* target_backend = (TexBackend*)target_tex->backend;
            if (target_backend) {
                vkCmdClearColorImage(cb, target_backend->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            }
        }
        return;
    }

    framegen_ensure_pipeline();

    // Update uniform buffer with VP matrices
    struct { float prev_vp[4][4]; float curr_vp[4][4]; float gen_vp[4][4]; float extent[2]; } *ub_data;
    VkSpan ub_span = vk_transient(sizeof(*ub_data), 256);
    ub_data = ub_span.ptr;
    if (!ub_data) return;

    memcpy(ub_data->prev_vp, fg_prev_vp, sizeof fg_prev_vp);
    extern void framegen_get_current_vp(float out[4][4]);
    framegen_get_current_vp(ub_data->curr_vp);
    memcpy(ub_data->gen_vp, fg_gen_vp, sizeof fg_gen_vp);
    ub_data->extent[0] = (float)fg_extent.width;
    ub_data->extent[1] = (float)fg_extent.height;

    // Update descriptor set with previous frame's color/depth + uniform buffer
    framegen_update_descriptors(VK_NULL_HANDLE, ub_span.buf, ub_span.off, sizeof(*ub_data));

    // Transition depth image from DEPTH_STENCIL_ATTACHMENT to SHADER_READ_ONLY
    VkImageMemoryBarrier2 depth_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = fg_prev_depth_image,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};

    VkDependencyInfo dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .dependencyFlags = 0,
                                 .imageMemoryBarrierCount = 1,
                                 .pImageMemoryBarriers = &depth_barrier};
    vkCmdPipelineBarrier2(cb, &dep_info);

    // Update descriptor set with previous frame's color/depth + uniform buffer
    framegen_update_descriptors(VK_NULL_HANDLE, ub_span.buf, ub_span.off, sizeof(*ub_data));

    // Render the warp
    VkRenderingAttachmentInfo color_att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                           .imageView = target_view,
                                           .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                           .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, extent},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &color_att,
                          .pDepthAttachment = NULL,
                          .pStencilAttachment = NULL};
    vk_begin_rendering(cb, &ri, &target_fmt, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, fg_pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, fg_pipeline_layout, 0, 1, &fg_desc_set, 0, NULL);
    
    VkViewport viewport = {0, 0, (float)extent.width, (float)extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, extent};
    vkCmdSetViewport(cb, 0, 1, &viewport);
    vkCmdSetScissor(cb, 0, 1, &scissor);
    
    vkCmdDraw(cb, 4, 1, 0, 0);
    
    vk_end_rendering(cb);

    // Transition depth image back to DEPTH_STENCIL_ATTACHMENT for next frame
    VkImageMemoryBarrier2 depth_barrier_back = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = fg_prev_depth_image,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};

    VkDependencyInfo dep_info_back = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .dependencyFlags = 0,
                                      .imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &depth_barrier_back};
    vkCmdPipelineBarrier2(cb, &dep_info_back);
}