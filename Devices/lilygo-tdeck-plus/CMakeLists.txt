file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    REQUIRES Tactility driver lilygo-module audio-codec-module es7210-module dummy-i2s-amp-module audio-stream-module
)
