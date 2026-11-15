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

// Framegen resources
static VkPipelineLayout fg_pipeline_layout;
static VkPipeline fg_pipeline;
static VkDescriptorSetLayout fg_desc_set_layout;
static VkDescriptorSet fg_desc_set;
static VkDescriptorPool fg_desc_pool;

// Previous frame's resources (for warp)
static VkImage fg_prev_color_image;
static VkImageView fg_prev_color_view;
static VkImage fg_prev_depth_image;
static VkImageView fg_prev_depth_view;
static VkFormat fg_color_format;
static VkFormat fg_depth_format;
static VkExtent2D fg_extent;
static float fg_prev_vp[4][4];
static bool fg_has_prev_frame;

// Camera for generated frame (half-step ahead)
static float fg_gen_vp[4][4];

FramegenCam g_framegen_cam;

void framegen_init(void) {
    g_framegen_cam.valid = false;
    fg_has_prev_frame = false;
    fg_pipeline_layout = VK_NULL_HANDLE;
    fg_pipeline = VK_NULL_HANDLE;
    fg_desc_set_layout = VK_NULL_HANDLE;
    fg_desc_set = VK_NULL_HANDLE;
    fg_desc_pool = VK_NULL_HANDLE;
    fg_prev_color_image = VK_NULL_HANDLE;
    fg_prev_color_view = VK_NULL_HANDLE;
    fg_prev_depth_image = VK_NULL_HANDLE;
    fg_prev_depth_view = VK_NULL_HANDLE;
    fg_color_format = VK_FORMAT_UNDEFINED;
    fg_depth_format = VK_FORMAT_UNDEFINED;
}

void framegen_camera(const float *u, uint64_t size) {
    if (!u) return;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) g_framegen_cam.vp[r][c] = u[r * 4 + c];
    g_framegen_cam.size = size;
    g_framegen_cam.valid = true;
}

void framegen_link(const float *p, uint64_t size) { (void)p; (void)size; }  // (the player's position: not yet read)

bool framegen_enabled(void) { 
    static int logged = -1;
    if (logged < 0) {
        LOG("[framegen] framegen_enabled: g_settings.framegen=%d\n", g_settings.framegen);
        logged = 1;
    }
    return g_settings.framegen; 
}  // (Video > Frame generation: off by default)

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

// Create framegen pipeline and resources
static void framegen_ensure_pipeline(void) {
    if (fg_pipeline) return;

    // Descriptor set layout: binding 0 = prev color, binding 1 = prev depth, binding 2 = uniform (VP matrices)
    VkDescriptorSetLayoutBinding bindings[3] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = NULL},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = NULL},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = NULL},
    };
    VkDescriptorSetLayoutCreateInfo dsl_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                .bindingCount = 3, .pBindings = bindings};
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &dsl_info, NULL, &fg_desc_set_layout));

    // Pipeline layout
    VkPipelineLayoutCreateInfo pl_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                          .setLayoutCount = 1, .pSetLayouts = &fg_desc_set_layout};
    VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &pl_info, NULL, &fg_pipeline_layout));

    // Descriptor pool
    VkDescriptorPoolSize pool_sizes[2] = {
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2},
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1},
    };
    VkDescriptorPoolCreateInfo dp_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                          .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = pool_sizes};
    VK_CHECK(vkCreateDescriptorPool(g_vk.dev, &dp_info, NULL, &fg_desc_pool));

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool = fg_desc_pool, .descriptorSetCount = 1,
                                              .pSetLayouts = &fg_desc_set_layout};
    VK_CHECK(vkAllocateDescriptorSets(g_vk.dev, &alloc_info, &fg_desc_set));

    // Shader modules
    size_t nv, nf;
    uint32_t *sv = vk_glsl_to_spirv(
        "#version 450\n"
        "layout(location = 0) in vec2 in_pos;\n"
        "layout(location = 0) out vec2 out_uv;\n"
        "void main() {\n"
        "   out_uv = in_pos * 0.5 + 0.5;\n"
        "   gl_Position = vec4(in_pos, 0.0, 1.0);\n"
        "}\n", STAGE_VERTEX, "fg_vert", &nv);
    uint32_t *sf = vk_glsl_to_spirv(
        "#version 450\n"
        "layout(set = 0, binding = 0) uniform sampler2D prev_color;\n"
        "layout(set = 0, binding = 1) uniform sampler2D prev_depth;\n"
        "layout(set = 0, binding = 2) uniform Uniforms {\n"
        "   mat4 prev_vp;\n"
        "   mat4 curr_vp;\n"
        "   mat4 gen_vp;\n"
        "   vec2 extent;\n"
        "} ub;\n"
        "layout(location = 0) in vec2 in_uv;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "float linearize_depth(float d) {\n"
        "   // Reverse-Z linearization\n"
        "   return 1.0 / (d * (1.0 - 1.0/1000.0) + 1.0/1000.0);\n"
        "}\n"
        "void main() {\n"
        "   float depth = texture(prev_depth, in_uv).r;\n"
        "   depth = linearize_depth(depth);\n"
        "   // Reconstruct world position from depth\n"
        "   vec4 clip = vec4(in_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
        "   vec4 world = inverse(ub.prev_vp) * clip;\n"
        "   world /= world.w;\n"
        "   // Project with generated VP\n"
        "   vec4 gen_clip = ub.gen_vp * world;\n"
        "   vec2 gen_uv = (gen_clip.xy / gen_clip.w) * 0.5 + 0.5;\n"
        "   // Sample previous color at warped position\n"
        "   out_color = texture(prev_color, gen_uv);\n"
        "}\n", STAGE_FRAGMENT, "fg_frag", &nf);

    VkShaderModule vs = vk_module(sv, nv);
    VkShaderModule fs = vk_module(sf, nf);
    free(sv);
    free(sf);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fs, .pName = "main"},
    };

    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                            .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                 .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cb = {.blendEnable = VK_FALSE,
                                              .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                   .attachmentCount = 1, .pAttachments = &cb};
    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                           .dynamicStateCount = 2, .pDynamicStates = dyn_states};

    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                       .stageCount = 2, .pStages = stages,
                                       .pVertexInputState = &vi, .pInputAssemblyState = &ia,
                                       .pViewportState = &vp, .pRasterizationState = &rs,
                                       .pMultisampleState = &ms, .pColorBlendState = &cb_info,
                                       .pDynamicState = &ds, .layout = fg_pipeline_layout,
                                       .renderPass = VK_NULL_HANDLE, .subpass = 0};
    VK_CHECK(vkCreateGraphicsPipelines(g_vk.dev, VK_NULL_HANDLE, 1, &gp, NULL, &fg_pipeline));

    vkDestroyShaderModule(g_vk.dev, vs, NULL);
    vkDestroyShaderModule(g_vk.dev, fs, NULL);
}

// Update descriptor set with current frame's resources
static void framegen_update_descriptors(VkDeviceMemory ub_mem, VkBuffer ub_buffer, VkDeviceSize ub_offset, VkDeviceSize ub_size) {
    VkDescriptorImageInfo color_info = {.sampler = VK_NULL_HANDLE, .imageView = fg_prev_color_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depth_info = {.sampler = VK_NULL_HANDLE, .imageView = fg_prev_depth_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo ub_info = {.buffer = ub_buffer, .offset = ub_offset, .range = ub_size};

    VkWriteDescriptorSet writes[3] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 0,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &color_info},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 1,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &depth_info},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 2,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &ub_info},
    };
    vkUpdateDescriptorSets(g_vk.dev, 3, writes, 0, NULL);
}

// Render the generated frame (warp)
void framegen_render_generated(VkCommandBuffer cb, VkImageView target_view, VkFormat target_fmt, VkExtent2D extent) {
    if (!framegen_enabled() || !framepace_gen_doubled() || !fg_has_prev_frame) return;
    if (!fg_prev_color_view || !fg_prev_depth_view) return;

    framegen_ensure_pipeline();

    // Update uniform buffer with VP matrices
    struct { float prev_vp[4][4]; float curr_vp[4][4]; float gen_vp[4][4]; float extent[2]; } *ub_data;
    VkSpan ub_span = vk_transient(sizeof(*ub_data), 256);
    ub_data = ub_span.ptr;
    if (!ub_data) return;

    memcpy(ub_data->prev_vp, fg_prev_vp, sizeof fg_prev_vp);
    // Current VP is the last real frame's
    extern void framegen_get_current_vp(float out[4][4]);
    framegen_get_current_vp(ub_data->curr_vp);
    memcpy(ub_data->gen_vp, fg_gen_vp, sizeof fg_gen_vp);
    ub_data->extent[0] = (float)fg_extent.width;
    ub_data->extent[1] = (float)fg_extent.height;

    framegen_update_descriptors(VK_NULL_HANDLE, ub_span.buf, ub_span.off, sizeof(*ub_data));

    // Render
    VkRenderingAttachmentInfo color_att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                           .imageView = fg_prev_color_view, // render to previous color? No, render to target
                                           .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                           .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    // We need to render to the target view passed in
    // For simplicity, we'll use the target_view directly
    // Actually we need to create a framebuffer/rendering for the target

    // This is a simplified implementation - in reality we'd need to set up proper rendering
    // For now, return early as the full implementation requires more integration
    (void)cb; (void)target_view; (void)target_fmt; (void)extent;
}

void framegen_get_current_vp(float out[4][4]) {
    if (g_framegen_cam.valid) {
        memcpy(out, g_framegen_cam.vp, sizeof g_framegen_cam.vp);
    } else {
        // Identity as fallback
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[r][c] = (r == c) ? 1.0f : 0.0f;
    }
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
        vkCmdClearColorImage(cb, target_view, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
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
    VkDescriptorImageInfo color_info = {.sampler = VK_NULL_HANDLE, .imageView = fg_prev_color_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depth_info = {.sampler = VK_NULL_HANDLE, .imageView = fg_prev_depth_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo ub_info = {.buffer = ub_span.buf, .offset = ub_span.off, .range = sizeof(*ub_data)};

    VkWriteDescriptorSet writes[3] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 0,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &color_info},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 1,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &depth_info},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fg_desc_set, .dstBinding = 2,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &ub_info},
    };
    vkUpdateDescriptorSets(g_vk.dev, 3, writes, 0, NULL);

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
}