// SPDX-License-Identifier: Apache-2.0
#include <coreutils/module.h>

#include <app/manager.h>
#include <app/manifest.h>

#include <tactility/module.h>

namespace coreutils {
    namespace cat { extern const ::AppManifest manifest; }
    namespace clear { extern const ::AppManifest manifest; }
    namespace cp { extern const ::AppManifest manifest; }
    namespace date { extern const ::AppManifest manifest; }
    namespace df { extern const ::AppManifest manifest; }
    namespace du { extern const ::AppManifest manifest; }
    namespace free { extern const ::AppManifest manifest; }
    namespace head { extern const ::AppManifest manifest; }
    namespace help { extern const ::AppManifest manifest; }
    namespace ls { extern const ::AppManifest manifest; }
    namespace mkdir { extern const ::AppManifest manifest; }
    namespace mv { extern const ::AppManifest manifest; }
    namespace printf { extern const ::AppManifest manifest; }
    namespace rm { extern const ::AppManifest manifest; }
    namespace tail { extern const ::AppManifest manifest; }
    namespace touch { extern const ::AppManifest manifest; }
    namespace wc { extern const ::AppManifest manifest; }
    namespace which { extern const ::AppManifest manifest; }
}

extern "C" {

static error_t start() {
    error_t error;
    if ((error = app_manager_add(&coreutils::cat::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::clear::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::cp::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::date::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::df::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::du::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::free::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::head::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::help::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::ls::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::mkdir::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::mv::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::printf::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::rm::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::tail::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::touch::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::wc::manifest)) != ERROR_NONE) return error;
    if ((error = app_manager_add(&coreutils::which::manifest)) != ERROR_NONE) return error;
    return ERROR_NONE;
}

static error_t stop() {
    app_manager_remove(coreutils::cat::manifest.id);
    app_manager_remove(coreutils::clear::manifest.id);
    app_manager_remove(coreutils::cp::manifest.id);
    app_manager_remove(coreutils::date::manifest.id);
    app_manager_remove(coreutils::df::manifest.id);
    app_manager_remove(coreutils::du::manifest.id);
    app_manager_remove(coreutils::free::manifest.id);
    app_manager_remove(coreutils::head::manifest.id);
    app_manager_remove(coreutils::help::manifest.id);
    app_manager_remove(coreutils::ls::manifest.id);
    app_manager_remove(coreutils::mkdir::manifest.id);
    app_manager_remove(coreutils::mv::manifest.id);
    app_manager_remove(coreutils::printf::manifest.id);
    app_manager_remove(coreutils::rm::manifest.id);
    app_manager_remove(coreutils::tail::manifest.id);
    app_manager_remove(coreutils::touch::manifest.id);
    app_manager_remove(coreutils::wc::manifest.id);
    app_manager_remove(coreutils::which::manifest.id);
    return ERROR_NONE;
}

Module coreutils_module = {
    .name = "coreutils",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr,
};

}
