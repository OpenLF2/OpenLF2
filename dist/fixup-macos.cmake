if(NOT DEFINED BUNDLE_PATH OR NOT DEFINED DEPLOY_ROOT)
    message(FATAL_ERROR "Pass BUNDLE_PATH and DEPLOY_ROOT")
endif()

set(executable "${BUNDLE_PATH}/Contents/MacOS/openlf2")
execute_process(COMMAND otool -l "${executable}"
    OUTPUT_VARIABLE load_commands RESULT_VARIABLE inspection_status)
if(NOT inspection_status EQUAL 0 OR
        NOT load_commands MATCHES "@executable_path/\\.\\./Frameworks")
    message(FATAL_ERROR "The macOS app is missing its Frameworks runtime path")
endif()

set(frameworks "${BUNDLE_PATH}/Contents/Frameworks")
file(MAKE_DIRECTORY "${frameworks}")
file(GLOB_RECURSE runtime_libraries LIST_DIRECTORIES FALSE "${DEPLOY_ROOT}/*.dylib")
if(NOT runtime_libraries)
    message(FATAL_ERROR "Conan did not deploy any macOS dylibs")
endif()
foreach(library IN LISTS runtime_libraries)
    file(COPY "${library}" DESTINATION "${frameworks}" FOLLOW_SYMLINK_CHAIN)
endforeach()
