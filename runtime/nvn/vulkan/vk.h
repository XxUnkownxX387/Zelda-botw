// Vulkan backend for the NVN layer (Windows; macOS uses runtime/nvn/metal.m + renderer.m).
// Files: device.c (instance, window, swapchain, present, screenshots, ImGui hooks), textures.c (images, uploads,
// views), exec.c (command execution: passes, draws, clears, copies, compute), shaders.c (GLSL -> SPIR-V, programs,
// pipelines). Mirrors the Metal backend's behaviour; differences are noted where they happen.
//
// Synchronisation model: every image lives in VK_IMAGE_LAYOUT_GENERAL, and a full memory barrier separates
// render passes, copies and dispatches (Metal tracks these hazards itself). Submissions are numbered; a timeline
// semaphore signals each number, which is what NVN fences (renderer_fence_seq / renderer_wait_seq) wait for.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __ANDROID__
#include <volk.h>  // (the Vulkan functions come from the driver android_driver.c loads)
#else
#include <vulkan/vulkan.h>
#endif
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>  // (Debian / Ubuntu libvulkan-memory-allocator-dev)
#endif

#include "../nvn.h"
#include "../maxwell/shader.h"

void vk_device_fault_report(void);
#ifdef __ANDROID__
void vk_android_load(void);  // android_driver.c: the Vulkan driver (adrenotools / system) into volk
const char *vk_driver_rejected(void);  // (the chosen driver package the system's stood in for; "" none)
void vk_wait_ready(void);    // device.c: until the device exists (the window's thread makes it)
// Mali (ARM Immortalis Mali, the Mali-G720 family): the Mali GPUs (vendorID 0x13B5) - their own options are only
// shown when it is the GPU in use (device.c)
bool vk_mali_detected(void);
#endif
#define VK_CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { if (r_ == VK_ERROR_DEVICE_LOST) vk_device_fault_report(); \
    FATAL("%s failed: %d (%s:%d)\n", #x, r_, __FILE__, __LINE__); } } while (0)
// Memory strategies (the explicit policy the backend runs on, replacing the runtime heuristics):
//   VKM_STRATEGY_CPU_COMPAT - CPU Compatibility: no host import, no exported file (fd / dma-buf) - global memory through CPU
//                    copies; Mali's mode to start from until the driver + capabilities validate a real import.
//   VKM_STRATEGY_HOST_IMPORT - the guest heap imported at HEAP_BASE as GPU memory (VK_EXT_external_memory_host).
//   VKM_STRATEGY_PLACED      - the guest heap mapped as GPU memory (VK_EXT_map_memory_placed).
//   VKM_STRATEGY_FILE        - the guest heap through an exported file / dma-buf (VK_KHR_external_memory_fd / VK_EXT_external_memory_dma_buf).
//   VKM_STRATEGY_AUTO        - start from VKM_STRATEGY_CPU_COMPAT (the Mali's starting mode) and move to a real strategy once the
//                    capabilities and the driver validate it; stay on CPU Compatibility otherwise.
enum { VKM_STRATEGY_CPU_COMPAT = 0, VKM_STRATEGY_HOST_IMPORT = 1, VKM_STRATEGY_PLACED = 2, VKM_STRATEGY_FILE = 3, VKM_STRATEGY_AUTO = 4 };
// The most swapchain images the renderer keeps (one view and one present semaphore each): it asks for few, but drivers
// may make more - a Mali-G57's made 12 (issue #1)
#define VK_MAX_SWAP_IMAGES 16

typedef struct {
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;
    VmaAllocator vma;
    VkPhysicalDeviceProperties props;
    VkPipelineCache pcache;
    VkSemaphore timeline;      // value = last completed submission number
    VkCommandPool pool;        // render thread only
    bool custom_border, divisor, host_import, shader_int64;
    // Explicit memory strategy (the policy this backend runs on; device.c / exec.c): CPU Compatibility (a Mali's
    // fallback mode: no host import, no exported file - fd / dma-buf - and no implicit setenv, global memory through
    // CPU copies), a real import / placed / file, or AUTO to start from CPU Compatibility and move once the driver
    // and the capabilities validate it.
    int mem_strategy;
    bool dyn_render;              // VK_KHR_dynamic_rendering in use; without it, classic render passes (rpcompat.c)
    bool sync2;                   // synchronization2 in use; without it, Vulkan 1.0 barriers (device.c sync1_*)
    bool eds;                     // extended dynamic state in use; without it, those states (and extended dynamic state
                                  // 2's) baked into pipelines (exec.c PipeKey.fx)
    bool eds2;                    // extended dynamic state 2 in use; without it, depth bias and primitive restart are
                                  // pipeline state (exec.c PipeKey.prim_restart)
    bool fragment_shading_rate; // VK_KHR_fragment_shading_rate in use: a reduced fragment shading rate (exec.c post passes)
    bool fsr_2x2;                // VK_KHR_fragment_shading_rate: the device supports a 2x2 fragment shading rate (exec.c post passes)
    int mali_driver_major;   // the Mali driver version (VK_KHR_driver_properties driverInfo, "rNNpM") - Mali-G720 / Immortalis Gen4
    int mali_driver_minor;   // the Mali driver version's minor part (VK_KHR_driver_properties driverInfo, "rNNpM")
    bool texture_compression_astc_hdr; // VK_EXT_texture_compression_astc_hdr: hardware ASTC HDR decoding
#ifdef __ANDROID__
    bool map_placed;              // VK_EXT_map_memory_placed: the guest heap mapped as GPU memory (exec.c vk_heap_place)
    bool mem_fd;                  // VK_KHR_external_memory_fd: the same through an exported file mapped there (vk_heap_place)
    bool mem_dmabuf;              // VK_EXT_external_memory_dma_buf: that file as a dma-buf, when the opaque one won't do
    bool feedback_loop_layout;    // VK_EXT_attachment_feedback_loop_layout: draw reads target it writes in place (resolve_feedback)
    uint64_t placed_align;
    bool exec_stats;              // (Android, LARECOMP_SHADER_STATS=1) the compiler's statistics of each pipeline, logged
    uint32_t api;                 // the device's Vulkan version as used: 1.3, or 1.1 / 1.2 with extensions (device.c)
#endif
    // window
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat sc_format;
    VkExtent2D sc_extent;
    uint32_t sc_count;
    VkImage sc_images[VK_MAX_SWAP_IMAGES];
    VkImageView sc_views[VK_MAX_SWAP_IMAGES];
} VkG;
extern VkG g_vk;

// ---- rendering: dynamic rendering, or classic render passes without it (rpcompat.c)
// colors / ds_format: the attachments' view formats (VK_FORMAT_UNDEFINED: an unused slot), as the pipelines get them
void vk_begin_rendering(VkCommandBuffer cb, const VkRenderingInfo *ri, const VkFormat *colors, VkFormat ds_format,
                        VkSampleCountFlagBits samples);
void vk_end_rendering(VkCommandBuffer cb);
// A render pass a pipeline is made against (VK_NULL_HANDLE with dynamic rendering: VkPipelineRenderingCreateInfo then)
VkRenderPass vk_rp_compat(uint32_t ncolor, const VkFormat *colors, VkFormat depth, VkFormat stencil, VkSampleCountFlagBits samples);
void vk_rp_forget_view(VkImageView v);  // (a view destroyed: the framebuffers holding it go)
void vk_rp_collect(uint64_t done_seq);

// ---- submissions (exec.c)
// Work is recorded into the current command buffer; uploads go into a separate one that runs first.
VkCommandBuffer vk_cmd(void);         // current command buffer (outside any rendering unless exec.c began one)
VkCommandBuffer vk_upload_cmd(void);  // upload command buffer of the pending submission
uint64_t vk_pending_seq(void);        // number the pending submission will get
uint64_t vk_completed_seq(void);
void vk_wait_seq(uint64_t seq);
void vk_full_barrier(VkCommandBuffer cb);
void vk_end_pass(void);
void vk_submit_extra(VkSemaphore wait, VkSemaphore signal, VkCommandBuffer extra);  // submit + one more cb (present)

// Transient GPU-visible memory that lives until the submission it was allocated for completes.
typedef struct { VkBuffer buf; uint64_t off; void *ptr; } VkSpan;
VkSpan vk_transient(uint64_t size, uint64_t align);
VkSpan vk_transient_copy(const void *data, uint64_t size, uint64_t align);
VkSpan vk_readback(uint64_t size);  // host-cached GPU -> CPU staging (read right after the copy completes)
void vk_readback_ready(uint64_t size);  // after the copy's submission is done, before the CPU reads it
VkDescriptorSet vk_alloc_set(VkDescriptorSetLayout layout);

// ---- textures (textures.c)
enum { RS_NONE = 0, RS_SCENE = 1, RS_OUT = 2 };  // render-target scale groups (textures.c)
typedef struct TexBackend {
    VkImage image;
    VmaAllocation mem;
    VkFormat format;
    VkImageViewType base_type;
    uint32_t width, height, depth, layers, levels, samples;
    VkImageAspectFlags aspect;
    bool gpu_owned, uploaded, dynamic;
    uint64_t data_hash, sample_hash, checked_frame, full_frame;
    uint32_t wgen;  // page write tracking generation at the last upload check (textures.c)
    uint8_t skip;   // guest levels left out at the top (half-size textures on low RAM: textures.c create_image)
    bool scalable, presented;
    uint8_t rs_group;  // RS_*: which output scale its allocation follows (textures.c)
    uint64_t touched_frame, frame_cleared;
    uint64_t rt_frame;  // the last frame a draw rendered into it, + 1 (0: never) - exec.c rt_one_off
    bool rt_oneoff;     // (that frame's draws into it: not drawn into the frame before)
    uint32_t level_gen;
    float sx, sy;
    void *pending_rgba;  // host-produced pixels (video) waiting to be uploaded on the render thread
    uint32_t pending_w, pending_h, pending_stride;
    uint8_t astc_bw, astc_bh;  // ASTC without GPU support: decoded to RGBA8 on upload (runtime/nvn/astc.c)
    bool astc_srgb;
    VkFormat bc_vf;  // (Android) a BC format the GPU lacks (Mali): decoded on upload into b->format (textures.c)
    bool bc3_astc_ok;  // (Android) BC3 texture verified fully opaque -> can use ASTC 4x4
    struct BcShadow *bc_shadow;  // (bc_vf) drawn into through a view of the block's size: the blocks (textures.c)
    VkFormat view_alt;  // (a render target made without MUTABLE_FORMAT: textures.c rt_formats) the one other format its
    bool fixed_fmt;     // views may have - its sRGB / UNORM twin, or none
    int tp;              // runtime/nvn/texpatch.c: 0 not checked yet, -1 none, else known UI texture + 1
    uint32_t tp_gen;     // texpatch_generation() it was last uploaded with
    int reg;  // slot in textures.c's live list + 1 (a reboot destroys them all)
    uint64_t last_used_frame;  // VRAM budget LRU: frame when this texture was last accessed
    uint64_t mem_size;         // VRAM budget: GPU memory size of this texture (bytes)
} TexBackend;

TexBackend *vk_tex(NvnTexture *t, bool for_render);  // backend image (uploads CPU contents unless for_render)
void vk_tex_upload_for_mips(NvnTexture *t);  // (drawn into at a level past 0: the CPU's contents first, if never uploaded)
VkFormat vk_format(uint32_t nvn_format);
bool vk_format_alpha_one(uint32_t nvn_format);
bool renderer_texture_scalable(NvnTexture *t);
uint8_t vk_texture_scale_group(NvnTexture *t);
void vk_make_scalable_like(NvnTexture *t, NvnTexture *like);  // (a pass's attachment the size of a scaled one)
void renderer_get_scene_scale(float *sx, float *sy);
void renderer_set_scene_scale(float sx, float sy);
// View of t (optionally through an NVN texture view) for rendering or sampling.
VkImageView vk_view(NvnTexture *t, const NvnTextureView *v, bool has_view, bool render, int want_textype);
VkImageView vk_image_view(TexBackend *b, VkImageViewType type, VkFormat fmt, uint32_t lv0, uint32_t nlv, uint32_t ly0,
                          uint32_t nly, VkComponentMapping sw, VkImageAspectFlags aspect);
VkImageView vk_dummy_view(int textype);
// Framegen capture is now done at end of main scene pass (vk_end_pass in exec.c)
// VkImageView framegen_get_depth_view(void);
// NvnTexture* framegen_get_depth_texture(void);
// Returns the current swapchain texture being presented (for framegen clear fallback)
NvnTexture* framegen_get_target_texture(void);
#ifdef __ANDROID__
// A render target view of a CPU-decoded BC texture in a format of the block's size (GPU compression): a view of its
// shadow image (*fmt, *w x *h: that format and level extent), else VK_NULL_HANDLE. vk_bc_shadows_resolve: the blocks
// drawn decoded into the textures, but those the next pass draws into in their shadow's format (targets through
// views[i] when has_view[i]; outside any pass, before anything reads them; waits for the GPU).
VkImageView vk_bc_shadow_view(NvnTexture *t, const NvnTextureView *v, VkFormat *fmt, uint32_t *w, uint32_t *h);
void vk_bc_shadows_resolve(NvnTexture *const *targets, const NvnTextureView *views, const uint8_t *has_view, int ntargets);
#endif
VkSampler vk_sampler(NvnSamplerDesc *d);
VkSampler vk_sampler_plain(NvnSamplerDesc *d);  // (the same without depth comparison: for non-shadow lookups)
VkSampler vk_default_sampler(bool linear);
void vk_image_init_layout(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect);
// Destruction once every submission that may use the object has completed.
void vk_defer_image(VkImage img, VmaAllocation mem, uint64_t mem_size);
void vk_defer_view(VkImageView v);
void vk_views_forget(TexBackend *b);  // drop (deferred) the cached views of an image being replaced
uint64_t vk_frame_counter(void);
// VRAM budget eviction (textures.c): call once per frame at present time, on render thread, GPU drained
void vram_budget_evict_if_needed(void);
// Pipeline cache (device.c): persistent shader cache in <data root>/pipeline_cache.bin
void vk_pipeline_cache_save(void);                      // immediate save (called on pause)
void vk_pipeline_cache_start_periodic_save(void);       // start 5-min periodic save thread
void vk_pipeline_cache_stop_periodic_save(void);        // stop periodic save thread (joins thread)
void vk_pipeline_cache_request_save(void);              // non-blocking save request (safe if device not ready)
void vk_pipeline_cache_clear(void);                     // atomic clear (not cancelled by in-memory save)
bool vk_pipeline_cache_is_compatible(void);             // check if cache file exists and is compatible

// ---- shaders / pipelines (shaders.c)
VkShaderModule vk_compile_glsl(const char *src, int stage, const char *name);  // NULL on failure (logged)
VkShaderModule vk_builtin_shader(const char *name);  // internal shaders (clear, copy, present)

// ---- device / window (device.c)
void vk_save_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h);

// A frame's work in ms (the longest of the game's thread, the render thread and the GPU; smoothed), -1 unknown
double vk_frame_work_ms(void);
// Low RAM mode (vulkan/exec.c): -1 automatic (devices up to 4 GB), 1 on, 0 off; read once as the app starts
bool vk_low_ram(void);

// Benchmark accessors (exec.c)
uint64_t vk_upload_bytes(void);
uint64_t vk_feedback_copies(void);
uint64_t vk_feedback_reads(void);
uint64_t vk_inplace_breaks(void);
uint64_t vk_pipe_binds(void);
uint64_t vk_set_binds(void);
uint64_t vk_cb_bytes(int which);
uint64_t vk_cb_copied(void);
uint64_t vk_cbwin(void);
uint64_t vk_submissions(void);
uint64_t vk_image_copies(void);
uint64_t vk_buffer_copies(void);
int vk_pss_mb(void);

// Benchmark accessors (device.c)
const char *vk_driver_version(void);
int vk_api_version(void);
int vk_mali_driver_major(void);
int vk_mali_driver_minor(void);
int vk_mem_strategy(void);

bool vk_fragment_shading_rate(void);
bool vk_astc_hdr(void);
bool vk_maintenance5(void);
bool vk_sync2(void);
bool vk_dynamic_rendering(void);
bool vk_external_memory_host(void);
bool vk_map_memory_placed(void);
bool vk_external_memory_fd(void);
bool vk_external_memory_dmabuf(void);
bool vk_feedback_loop_layout(void);
