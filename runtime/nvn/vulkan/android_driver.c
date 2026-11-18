// Android: which Vulkan driver the backend uses. LARECOMP_VK_DRIVER=<path of a driver library in the app's internal
// storage> (the activity sets it from the user's choice, e.g. Turnip): loaded through adrenotools, in a linker
// namespace of its own (its hooks are in nativeLibraryDir, LARECOMP_NATIVE_LIB_DIR). Otherwise, or if that fails, the
// system driver. volk then takes every Vulkan function from it (device.c: volkLoadInstance / volkLoadDevice).
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk.h"

// Does this driver offer a Vulkan GPU at all? (Turnip v26.0.0 R8, built for the A7xx: none on an Adreno 830 - the app
// stopped at "no Vulkan device" on every start, its settings out of reach.) 1.1 and up: below 1.3 device.c checks the
// extensions it needs instead (COMPAT_EXT).
static bool driver_has_gpu(PFN_vkGetInstanceProcAddr gipa) {
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!create) return false;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst;
    if (create(&ci, NULL, &inst) != VK_SUCCESS) return false;
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    bool ok = false;
    uint32_t n = 0;
    if (enumerate && props && enumerate(inst, &n, NULL) == VK_SUCCESS && n) {
        VkPhysicalDevice pd[8];
        if (n > 8) n = 8;
        if (enumerate(inst, &n, pd) >= 0)
            for (uint32_t i = 0; i < n && !ok; i++) {
                VkPhysicalDeviceProperties p;
                props(pd[i], &p);
                ok = p.apiVersion >= VK_API_VERSION_1_1;
            }
    }
    if (destroy) destroy(inst, NULL);
    return ok;
}

// The chosen driver the system's stood in for (its package's name; "" none): the start screen and the driver errors
// say so - before, a Turnip build for another Adreno generation fell back silently, and the "driver too old" error
// read as if Turnip lacked the extensions
static char g_rejected[128];
const char *vk_driver_rejected(void) { return g_rejected; }

// Check if the driver's GPU is Adreno (vendor 0x5143 = Qualcomm). Used to avoid loading Turnip on non-Adreno GPUs.
static bool driver_is_adreno(PFN_vkGetInstanceProcAddr gipa) {
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!create) return false;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst;
    if (create(&ci, NULL, &inst) != VK_SUCCESS) return false;
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    bool is_adreno = false;
    uint32_t n = 0;
    if (enumerate && props && enumerate(inst, &n, NULL) == VK_SUCCESS && n) {
        VkPhysicalDevice pd[8];
        if (n > 8) n = 8;
        if (enumerate(inst, &n, pd) >= 0)
            for (uint32_t i = 0; i < n && !is_adreno; i++) {
                VkPhysicalDeviceProperties p;
                props(pd[i], &p);
                if (p.vendorID == 0x5143) is_adreno = true;  // Qualcomm/Adreno
            }
    }
    if (destroy) destroy(inst, NULL);
    return is_adreno;
}

void vk_android_load(void) {
    void *lib = NULL;
    const char *drv = getenv("LARECOMP_VK_DRIVER"), *hooks = getenv("LARECOMP_NATIVE_LIB_DIR");
    
    // First, check the system driver to determine GPU vendor before attempting any custom driver
    void *sys_lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    bool is_mali = false;
    bool is_adreno = false;
    if (sys_lib) {
        PFN_vkGetInstanceProcAddr sys_gipa = (PFN_vkGetInstanceProcAddr)dlsym(sys_lib, "vkGetInstanceProcAddr");
        if (sys_gipa) {
            // Quick vendor check using system driver
            PFN_vkCreateInstance create = (PFN_vkCreateInstance)sys_gipa(NULL, "vkCreateInstance");
            if (create) {
                VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
                VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
                VkInstance inst;
                if (create(&ci, NULL, &inst) == VK_SUCCESS) {
                    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)sys_gipa(inst, "vkEnumeratePhysicalDevices");
                    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)sys_gipa(inst, "vkGetPhysicalDeviceProperties");
                    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)sys_gipa(inst, "vkDestroyInstance");
                    uint32_t n = 0;
                    if (enumerate && props && enumerate(inst, &n, NULL) == VK_SUCCESS && n) {
                        VkPhysicalDevice pd[8];
                        if (n > 8) n = 8;
                        if (enumerate(inst, &n, pd) >= 0) {
                            for (uint32_t i = 0; i < n; i++) {
                                VkPhysicalDeviceProperties p;
                                props(pd[i], &p);
                                if (p.vendorID == 0x13B5) is_mali = true;      // ARM Mali
                                else if (p.vendorID == 0x5143) is_adreno = true; // Qualcomm Adreno
                            }
                        }
                    }
                    if (destroy) destroy(inst, NULL);
                }
            }
        }
        dlclose(sys_lib);
    }
    
    // On Mali GPUs (like Mali-G720 MC7 on Xiaomi 15T), completely skip Turnip/adrenotools
    // and force use of the optimized native ARM Mali Vulkan driver
    if (is_mali) {
        if (drv && *drv && hooks && *hooks) {
            LOG("[vk] Mali GPU detected (vendor 0x13B5): ignoring custom driver request '%s', forcing native ARM Mali driver\n", drv);
        }
        drv = NULL; // Force system driver on Mali
    }
    
    if (drv && *drv && hooks && *hooks) {
        const char *slash = strrchr(drv, '/');
        char dir[1024];
        if (slash && (size_t)snprintf(dir, sizeof dir, "%.*s/", (int)(slash - drv), drv) < sizeof dir)
            lib = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hooks, dir, slash + 1,
                                             NULL, NULL);
        LOG("[vk] driver %s: %s\n", drv, lib ? "loaded through adrenotools" : "FAILED, using the system driver");
        PFN_vkGetInstanceProcAddr g = lib ? (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr") : NULL;
        if (lib && (!g || !driver_has_gpu(g))) {
            LOG("[vk] driver %s: no Vulkan 1.1+ GPU it supports here - using the system driver\n", drv);
            lib = NULL;
        }
        if (!lib) {
            const char *name = drv;
            if (slash) {
                const char *p = slash;
                while (p > drv && p[-1] != '/') p--;
                name = p;
            }
            snprintf(g_rejected, sizeof g_rejected, "%.*s", slash && name < slash ? (int)(slash - name) : (int)strlen(name), name);
        }
    }
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) FATAL("no Vulkan driver: %s\n", dlerror());
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    if (!gipa) FATAL("the Vulkan driver has no vkGetInstanceProcAddr\n");
    volkInitializeCustom(gipa);
}
