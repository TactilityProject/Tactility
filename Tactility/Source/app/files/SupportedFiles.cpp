#include <Tactility/StringUtils.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace tt::app::files {

constexpr auto* TAG = "Files";

constexpr auto text_file_extensions = std::to_array<std::string_view>({
    ".txt", ".md", ".rst", ".adoc", ".csv", ".tsv", ".log", ".diff", ".patch",

    ".sh", ".bash",
    ".awk", ".sed", ".tcl",
    ".c", ".h",
    ".py", ".lua", ".js",

    ".yaml", ".yml",
    ".json", ".json5",
    ".toml",
    ".ini", ".cfg", ".conf", ".config",
    ".properties",
    ".env",
    ".gitignore", ".gitattributes",

    ".html", ".htm",
    ".css", ".scss", ".sass", ".less",
    ".xml", ".svg",
});

bool isSupportedAppFile(const std::string& filename) {
    return filename.ends_with(".app");
}

bool isSupportedImageFile(const std::string& filename) {
    // Currently only the PNG library is built into Tactility
    return string::lowercase(filename).ends_with(".png");
}

bool isSupportedTextFile(const std::string& filename) {
    std::string filename_lower = string::lowercase(filename);
    return std::ranges::any_of(text_file_extensions, [&filename_lower](std::basic_string_view<char> extension) -> bool {
        return filename_lower.ends_with(extension);
    });
}

} // namespace tt::app::filebrowser
