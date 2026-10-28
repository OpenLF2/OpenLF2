# PS5 sources, link options and libraries for the openlf2 target.
target_compile_definitions(openlf2 PRIVATE OPENLF2_PS5=1)
target_sources(openlf2 PRIVATE src/adapters/ps5/entropy.cpp src/adapters/ps5/zstd_trace.cpp
    src/adapters/ps5/app_crt.cpp src/adapters/ps5/log.cpp src/adapters/ps5/input_stubs.cpp
    src/adapters/ps5/direct_memory.cpp src/adapters/ps5/startup.cpp)
target_link_options(openlf2 PRIVATE
    -nostartfiles
    "-Wl,-e,_start"
    # The SDK's weak libc dlopen shim points at these optional hooks. The
    # PS5 loader has no general dlopen service; preserve the weak-null
    # behavior instead of emitting unsupported imports into the title.
    "-Wl,--defsym=__dlopen=0"
    "-Wl,--defsym=__dlclose=0"
    "-Wl,--defsym=__dlsym=0"
    "-Wl,--defsym=__dladdr=0"
    "-Wl,--defsym=__dlerror=0"
    "-Wl,--version-script=${PROJECT_SOURCE_DIR}/cmake/ps5/symbols.map"
    "-Wl,-T,${PROJECT_SOURCE_DIR}/cmake/ps5/pie.ld")
set_property(TARGET openlf2 APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/ps5/symbols.map"
    "${PROJECT_SOURCE_DIR}/cmake/ps5/pie.ld")
