// SPDX-License-Identifier: Apache-2.0
#include <app/start.h>

#include <app/manifest.h>
#include <app/private/ledger.h>
#include <app/private/manager_internal.h>

#include <tactility/concurrent/mutex.h>

extern "C" {

AppStartContext app_start_context_for_manifest(const AppManifest* manifest) {
    AppStartContext context = {};
    context.manifest = manifest;
    context.location = manifest->location;
    context.stack = manifest->stack;
    return context;
}

AppStartContext app_start_context_for_location(AppLocation location) {
    AppStartContext context = {};
    context.manifest = nullptr;
    context.location = location;
    context.stack = {};
    return context;
}

error_t app_start_context_from_id(const char* id, AppStartContext* out_context) {
    auto& ledger = app_ledger();

    mutex_lock(&ledger.mutex);
    auto manifest_iterator = ledger.manifests.find(id);
    if (manifest_iterator == ledger.manifests.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    const AppManifest* manifest = manifest_iterator->second;
    mutex_unlock(&ledger.mutex);

    *out_context = app_start_context_for_manifest(manifest);
    return ERROR_NONE;
}

void app_start_context_set_stack(AppStartContext* context, AppStackConfig stack) {
    context->stack = stack;
}

void app_start_context_set_arguments_ext(AppStartContext* context, int argc, const char* const argv[]) {
    context->argc = argc;
    context->argv = argv;
}

void app_start_context_set_arguments(AppStartContext* context, const char* const arguments[]) {
    int argc = 0;
    if (arguments != nullptr) {
        while (arguments[argc] != nullptr) {
            argc++;
        }
    }
    context->argc = argc;
    context->argv = arguments;
}

void app_start_context_set_streams(AppStartContext* context, const AppStreamBinding* bindings, size_t binding_count) {
    context->bindings = bindings;
    context->binding_count = binding_count;
}

void app_start_context_set_parent(AppStartContext* context, AppInstanceId parent_id) {
    context->parent_id = parent_id;
}

void app_start_context_set_environment(AppStartContext* context, char* const environment[]) {
    context->environment = environment;
}

error_t app_start_with_context(AppStartContext* context, AppInstanceId* out_app_instance_id) {
    return app_manager_start_internal(context, out_app_instance_id);
}

error_t app_start(const char* id, int argc, const char* const argv[], AppInstanceId* out_app_instance_id) {
    AppStartContext context;
    error_t lookup_result = app_start_context_from_id(id, &context);
    if (lookup_result != ERROR_NONE) {
        return lookup_result;
    }
    app_start_context_set_arguments_ext(&context, argc, argv);
    return app_manager_start_internal(&context, out_app_instance_id);
}

error_t app_start_for_result(const char* id, int argc, const char* const argv[], AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id) {
    AppStartContext context;
    error_t lookup_result = app_start_context_from_id(id, &context);
    if (lookup_result != ERROR_NONE) {
        return lookup_result;
    }
    app_start_context_set_arguments_ext(&context, argc, argv);
    app_start_context_set_parent(&context, parent_instance_id);
    return app_manager_start_internal(&context, out_app_instance_id);
}

error_t app_start_with_streams(const char* id, const AppStreamBinding* bindings, size_t binding_count, AppInstanceId* out_app_instance_id) {
    AppStartContext context;
    error_t lookup_result = app_start_context_from_id(id, &context);
    if (lookup_result != ERROR_NONE) {
        return lookup_result;
    }
    app_start_context_set_streams(&context, bindings, binding_count);
    return app_manager_start_internal(&context, out_app_instance_id);
}

error_t app_start_for_result_with_streams(const char* id, int argc, const char* const argv[], const AppStreamBinding* bindings, size_t binding_count, AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id) {
    AppStartContext context;
    error_t lookup_result = app_start_context_from_id(id, &context);
    if (lookup_result != ERROR_NONE) {
        return lookup_result;
    }
    app_start_context_set_arguments_ext(&context, argc, argv);
    app_start_context_set_streams(&context, bindings, binding_count);
    app_start_context_set_parent(&context, parent_instance_id);
    return app_manager_start_internal(&context, out_app_instance_id);
}

} // extern "C"
