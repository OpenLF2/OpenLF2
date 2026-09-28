# SPDX-License-Identifier: MIT
# Copyright (c) 2026 OpenLF2 contributors

# Icons are rendered from the SVG via tools/icons/make_icons.py and never committed.
# OPENLF2_ICON_DIR lets packaging scripts pre-fill icons where Python isn't available.
set(OPENLF2_ICON_DIR "" CACHE PATH "Directory of icons rendered by tools/icons/make_icons.py (empty: render into the build tree)")
set(openlf2_icons "")
set(openlf2_icon_svg "${PROJECT_SOURCE_DIR}/dist/io.github.openlf2.OpenLF2.svg")
set(openlf2_icon_tool "${PROJECT_SOURCE_DIR}/tools/icons/make_icons.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${openlf2_icon_svg}" "${openlf2_icon_tool}")
if(OPENLF2_ICON_DIR)
    set(openlf2_icons "${OPENLF2_ICON_DIR}")
else()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${openlf2_icon_tool}" "${CMAKE_BINARY_DIR}/icons"
            RESULT_VARIABLE openlf2_icon_result ERROR_VARIABLE openlf2_icon_error)
        if(openlf2_icon_result EQUAL 0)
            set(openlf2_icons "${CMAKE_BINARY_DIR}/icons")
        else()
            message(WARNING "Icons were not generated: ${openlf2_icon_error}")
        endif()
    else()
        message(STATUS "Python 3 not found: building without icons")
    endif()
endif()
if(openlf2_icons AND NOT EXISTS "${openlf2_icons}/window_icon.hpp")
    message(WARNING "${openlf2_icons} holds no rendered icons; building without them")
    set(openlf2_icons "")
endif()
