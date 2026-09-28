# Debian builds have no LuaJIT CMake config; keep this finder inside the system provider.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LUAJIT QUIET luajit)
endif()
find_path(LuaJIT_INCLUDE_DIR NAMES luajit.h
    HINTS ${PC_LUAJIT_INCLUDE_DIRS} PATH_SUFFIXES luajit-2.1 luajit-2.0)
find_library(LuaJIT_LIBRARY NAMES luajit-5.1 luajit lua51
    HINTS ${PC_LUAJIT_LIBRARY_DIRS})
if(LuaJIT_INCLUDE_DIR)
    file(STRINGS "${LuaJIT_INCLUDE_DIR}/luajit.h" _version_line
        REGEX "^#define LUAJIT_VERSION[ \t]+")
    string(REGEX MATCH "[0-9]+[.][0-9]+([.][0-9]+)?" LuaJIT_VERSION "${_version_line}")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LuaJIT REQUIRED_VARS LuaJIT_LIBRARY LuaJIT_INCLUDE_DIR
    VERSION_VAR LuaJIT_VERSION)
if(LuaJIT_FOUND AND NOT TARGET LuaJIT::LuaJIT)
    add_library(LuaJIT::LuaJIT UNKNOWN IMPORTED)
    set_target_properties(LuaJIT::LuaJIT PROPERTIES IMPORTED_LOCATION "${LuaJIT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LuaJIT_INCLUDE_DIR}")
endif()
mark_as_advanced(LuaJIT_INCLUDE_DIR LuaJIT_LIBRARY)
