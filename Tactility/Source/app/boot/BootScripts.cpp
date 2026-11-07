#include <tactility/filesystem/fs.h>
#include <Tactility/app/boot/BootScripts.h>

#include <Tactility/file/File.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/delay.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/paths.h>

#include <app/manager.h>
#include <app/start.h>
#include <app/stream.h>

#include <string>
#include <unistd.h>
#include <vector>

namespace tt::app::boot {

constexpr auto* TAG = "BootScripts";

namespace {

/** Logs what a script writes, one log line per output line */
class ScriptOutputLog {
    char line[256];
    size_t length = 0;
    const char* scriptName;

    void flush() {
        if (length > 0) {
            line[length] = '\0';
            LOG_I(TAG, "[%s] %s", scriptName, line);
            length = 0;
        }
    }

public:
    explicit ScriptOutputLog(const char* scriptName) : scriptName(scriptName) {}

    void feed(const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; i++) {
            const char c = static_cast<char>(data[i]);
            if (c == '\n') {
                flush();
            } else if (c != '\r') {
                line[length++] = c;
                if (length == sizeof(line) - 1) {
                    flush();
                }
            }
        }
    }

    ~ScriptOutputLog() { flush(); }
};

/** Runs a script in the shell, and logs its output. Returns when the script has ended. */
void runBootScript(const std::string& path, const char* name) {
    constexpr size_t STREAM_BUFFER_SIZE = 512;
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* stdout_buffer = static_cast<uint8_t*>(memory_alloc_with_policy(STREAM_BUFFER_SIZE, &policy));
    auto* stderr_buffer = static_cast<uint8_t*>(memory_alloc_with_policy(STREAM_BUFFER_SIZE, &policy));
    if (stdout_buffer == nullptr || stderr_buffer == nullptr) {
        LOG_E(TAG, "Out of memory for script %s", name);
        memory_free(stdout_buffer);
        memory_free(stderr_buffer);
        return;
    }

    AppStartContext context {};
    if (app_start_context_from_id("shell", &context) != ERROR_NONE) {
        LOG_E(TAG, "Shell not available for script %s", name);
        memory_free(stdout_buffer);
        memory_free(stderr_buffer);
        return;
    }
    // The shell starts in the user data path. The script path is passed as $0, so it needs no quoting.
    const char* const arguments[] = { "shell", "-c", ". \"$0\"", path.c_str(), nullptr };
    app_start_context_set_arguments(&context, arguments);

    AppStream stdout_stream {};
    AppStream stderr_stream {};
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    AppStreamBinding bindings[] = {
        { STDOUT_FILENO, &stdout_stream, stdout_buffer, STREAM_BUFFER_SIZE, &event_group, {}, -1 },
        { STDERR_FILENO, &stderr_stream, stderr_buffer, STREAM_BUFFER_SIZE, &event_group, {}, -1 },
    };
    app_start_context_set_streams(&context, bindings, sizeof(bindings) / sizeof(bindings[0]));

    LOG_I(TAG, "Running %s", name);
    AppInstanceId instance_id = 0;
    if (app_start_with_context(&context, &instance_id) != ERROR_NONE) {
        LOG_E(TAG, "Failed to run %s", name);
    } else {
        ScriptOutputLog output(name);
        uint8_t buffer[128];
        const auto drain = [&] {
            size_t size;
            while ((size = app_stream_read(&stdout_stream, buffer, sizeof(buffer))) > 0) {
                output.feed(buffer, size);
            }
            while ((size = app_stream_read(&stderr_stream, buffer, sizeof(buffer))) > 0) {
                output.feed(buffer, size);
            }
        };
        while (app_manager_get_state(instance_id) != APP_INSTANCE_STATE_STOPPED) {
            drain();
            delay_millis(10);
        }
        drain();

        app_stream_unsubscribe(&stdout_stream);
        app_stream_unsubscribe(&stderr_stream);
        app_manager_stop(instance_id);
    }

    task_event_group_destruct(&event_group);
    memory_free(stdout_buffer);
    memory_free(stderr_buffer);
}

} // namespace

void runBootScripts() {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        return;
    }
    const std::string boot_path = std::string(data_path) + "/boot";
    if (!directory_exists(boot_path.c_str())) {
        return;
    }

    std::vector<dirent> entries;
    if (file::scandir(boot_path, entries, nullptr, file::direntSortAlphaAndType) < 0) {
        LOG_E(TAG, "Failed to read %s", boot_path.c_str());
        return;
    }

    for (const auto& entry : entries) {
        const std::string name = entry.d_name;
        if (!name.ends_with(".sh")) {
            continue;
        }
        const std::string path = file::getChildPath(boot_path, name);
        if (file_exists(path.c_str())) {
            runBootScript(path, name.c_str());
        }
    }
}

}
