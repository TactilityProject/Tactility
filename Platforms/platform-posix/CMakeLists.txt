cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

add_library(platform-posix OBJECT)
target_sources(platform-posix PRIVATE ${SOURCES})
#target_include_directories(platform-posix PUBLIC include/)
target_link_libraries(platform-posix PUBLIC TactilityKernel)

# Routes every pthread_attr_setstack() call to __wrap_pthread_attr_setstack() in source/pthread_stack_wrap.c,
# which no-ops it. see that file for why.
# --wrap is a GNU ld option; Apple's linker doesn't support it, so that file instead self-registers a dyld interpose
# for this symbol on Apple, needing no link flag. PUBLIC so every consumer of this OBJECT library
# (Tactility, and every test target that links platform-posix directly) gets it applied too.
if (NOT APPLE)
    target_link_options(platform-posix PUBLIC "-Wl,--wrap=pthread_attr_setstack")
endif ()
