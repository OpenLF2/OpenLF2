find_package(Git QUIET)
set(openlf2_git_hash "unknown")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${OPENLF2_SOURCE_DIR}"
        OUTPUT_VARIABLE openlf2_git_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE openlf2_git_result
        ERROR_QUIET)
    if(openlf2_git_result EQUAL 0)
        set(openlf2_git_hash "${openlf2_git_output}")
    endif()
endif()
file(CONFIGURE OUTPUT "${OPENLF2_BUILD_INFO}" CONTENT
    "#pragma once\n#define OPENLF2_BUILD_LABEL \"OpenLF2 ${OPENLF2_VERSION} (${openlf2_git_hash})\"\n")
