file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES TactilityKernel audio-codec-module dummy-i2s-amp-module pdm-mic-module audio-stream-module
)
