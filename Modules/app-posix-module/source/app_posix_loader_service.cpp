// SPDX-License-Identifier: Apache-2.0
#include <app_posix/malloc_wrap.h>
#include <app_posix/task_wrap.h>

#include <app/elf_check.h>
#include <app/loader.h>
#include <app/location.h>

#include <tactility/error.h>
#include <tactility/log.h>

#include <service/manager.h>

#include <dlfcn.h>
#include <sys/stat.h>

#ifndef __APPLE__
#include <link.h>
#endif

#include <new>
#include <string>

constexpr auto* TAG = "app_posix_loader";

namespace {

/** load()-allocated state, passed back through run()/unload(). */
struct PosixAppRuntime {
    void* handle = nullptr;
    // Address range of the loaded image, see app_posix_set_current_image()
    uintptr_t image_start = 0;
    uintptr_t image_end = 0;
};

#ifndef __APPLE__
struct ImageRangeSearch {
    ElfW(Addr) base;
    uintptr_t start;
    uintptr_t end;
};

int find_image_range(struct dl_phdr_info* info, size_t, void* context) {
    auto* search = static_cast<ImageRangeSearch*>(context);
    if (info->dlpi_addr != search->base) {
        return 0;
    }
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& header = info->dlpi_phdr[i];
        if (header.p_type != PT_LOAD) {
            continue;
        }
        const uintptr_t start = info->dlpi_addr + header.p_vaddr;
        const uintptr_t end = start + header.p_memsz;
        if (search->start == 0 || start < search->start) {
            search->start = start;
        }
        if (end > search->end) {
            search->end = end;
        }
    }
    return 1;
}

void resolve_image_range(PosixAppRuntime* runtime) {
    struct link_map* map = nullptr;
    if (dlinfo(runtime->handle, RTLD_DI_LINKMAP, &map) != 0 || map == nullptr) {
        return;
    }
    ImageRangeSearch search { .base = map->l_addr, .start = 0, .end = 0 };
    dl_iterate_phdr(find_image_range, &search);
    runtime->image_start = search.start;
    runtime->image_end = search.end;
}
#endif

bool is_regular_file(const std::string& path) {
    struct stat path_stat {};
    return ::stat(path.c_str(), &path_stat) == 0 && S_ISREG(path_stat.st_mode);
}

#ifndef __APPLE__
constexpr ElfRequirements EXECUTABLE_REQUIREMENTS = {
    .elf_class = ELF_CLASS_64,
    .data = ELF_DATA_2LSB,
    .types = ELF_TYPE_MASK(ELF_TYPE_DYN),
#if defined(__x86_64__)
    .machine = ELF_MACHINE_X86_64,
#elif defined(__aarch64__)
    .machine = ELF_MACHINE_AARCH64,
#else
#error "Unsupported POSIX architecture for ELF machine check"
#endif
};
#endif

// Validates an already-resolved binary path (see resolve_app_path()) before it's handed to
// dlopen(): the extension check is a cheap string comparison, so the file is only opened as a
// last resort.
bool is_executable_file(const std::string& resolved_path) {
    if (!resolved_path.ends_with(".so")) {
        return false;
    }
#ifdef __APPLE__
    // The simulator's app binaries are Mach-O on macOS, not ELF, so there is no header to check.
    return is_regular_file(resolved_path);
#else
    return elf_check_file(resolved_path.c_str(), &EXECUTABLE_REQUIREMENTS);
#endif
}

// location.location can be either an app's install directory or the .so file directly, mirroring
// app_esp32_loader_service.cpp's resolve_elf_path(). A "packaged" app's install directory always
// holds its single binary at the fixed path {dir}/bin/posix-{TACTILITY_POSIX_ARCH}/app.so - a
// "terminal" app has no such file (its several binaries keep their own names), so this correctly
// leaves it unresolvable - terminal apps aren't run through AppLoaderApi (see app/install.h).
error_t resolve_app_path(const std::string& path, std::string& resolvedPath) {
    if (path.ends_with(".so")) {
        resolvedPath = path;
        return ERROR_NONE;
    }
    std::string candidate = path + "/bin/posix-" TACTILITY_POSIX_ARCH "/app.so";
    if (!is_regular_file(candidate)) {
        return ERROR_NOT_FOUND;
    }
    resolvedPath = candidate;
    return ERROR_NONE;
}

error_t api_load(AppLocation location, AppRuntime* out_runtime) {
    if (location.type != APP_LOCATION_PATH) {
        LOG_E(TAG, "Unsupported location type");
        return ERROR_NOT_SUPPORTED;
    }

    std::string app_path;
    auto error = resolve_app_path(static_cast<const char*>(location.location), app_path);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve app path: %s", location.location);
        return error;
    }

    if (!is_executable_file(app_path)) {
        LOG_E(TAG, "Not executable: %s", app_path.c_str());
        return ERROR_NOT_ALLOWED;
    }

    LOG_I(TAG, "Loading %s", app_path.c_str());

    app_posix_install_task_hooks();

    void* handle = dlopen(app_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        LOG_E(TAG, "dlopen(%s) failed: %s", app_path.c_str(), dlerror());
        return ERROR_NOT_FOUND;
    }

    auto* runtime = new (std::nothrow) PosixAppRuntime { .handle = handle };
    if (runtime == nullptr) {
        LOG_E(TAG, "Out of memory");
        dlclose(handle);
        return ERROR_OUT_OF_MEMORY;
    }

#ifndef __APPLE__
    resolve_image_range(runtime);
#endif

    *out_runtime = runtime;
    return ERROR_NONE;
}

int32_t api_run(AppRuntime runtime_ptr, uint32_t /*app_instance_id*/, int argc, char* argv[]) {
    auto* runtime = static_cast<PosixAppRuntime*>(runtime_ptr);

    // Clear any pending error, per dlsym(3)'s own recommended idiom for telling a NULL symbol address apart from a real lookup failure
    dlerror();
    void* symbol = dlsym(runtime->handle, "main");
    const char* lookup_error = dlerror();
    if (symbol == nullptr || lookup_error != nullptr) {
        LOG_E(TAG, "dlsym(\"main\") failed: %s", lookup_error != nullptr ? lookup_error : "not found");
        return -1;
    }

    auto* main_fn = reinterpret_cast<AppMainFn>(symbol);
    app_posix_set_current_image(runtime->image_start, runtime->image_end);
    const int32_t result = main_fn(argc, argv);
    app_posix_set_current_image(0, 0);
    return result;
}

void api_unload(AppRuntime runtime_ptr) {
    auto* runtime = static_cast<PosixAppRuntime*>(runtime_ptr);
    // The image's static destructors run here, and what they free is the app's own (see app/memory.h)
    app_posix_set_current_image(runtime->image_start, runtime->image_end);
    dlclose(runtime->handle);
    app_posix_set_current_image(0, 0);
    delete runtime;
}

bool api_is_executable(AppLocation location) {
    if (location.type != APP_LOCATION_PATH) {
        return false;
    }

    std::string app_path;
    if (resolve_app_path(static_cast<const char*>(location.location), app_path) != ERROR_NONE) {
        return false;
    }

    return is_executable_file(app_path);
}

AppLoaderApi loader_api = {
    .load = api_load,
    .run = api_run,
    .unload = api_unload,
    .is_executable = api_is_executable,
};

void* create_service(const ServiceManifest*) {
    return &loader_api;
}

void destroy_service(const ServiceManifest*, void*) {
}

} // namespace

ServiceManifest loader_service_manifest = {
    .id = APP_LOADER_PATH_SERVICE_ID,
    .create_service = create_service,
    .destroy_service = destroy_service,
    .on_start = nullptr,
    .on_stop = nullptr,
};
