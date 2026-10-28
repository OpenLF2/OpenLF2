# Converts the openlf2 target to a signed PS5 title folder.
if(OPENLF2_PS5_NATIVE_TOOL)
    if(NOT EXISTS "${OPENLF2_PS5_NATIVE_TOOL}")
        message(FATAL_ERROR "OPENLF2_PS5_NATIVE_TOOL does not exist: ${OPENLF2_PS5_NATIVE_TOOL}")
    endif()
    if(NOT OPENLF2_PS5_TITLE_ID MATCHES "^PPSA[0-9][0-9][0-9][0-9][0-9]$"
        OR NOT OPENLF2_PS5_CONCEPT_ID MATCHES "^[0-9][0-9][0-9][0-9][0-9]$"
        OR NOT OPENLF2_PS5_CONTENT_SUFFIX MATCHES "^[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]$")
        message(FATAL_ERROR "PS5 title, concept, or content suffix has an invalid format")
    endif()
    if(NOT OPENLF2_PS5_RUNTIME_MODULE OR NOT EXISTS "${OPENLF2_PS5_RUNTIME_MODULE}")
        message(FATAL_ERROR "Set OPENLF2_PS5_RUNTIME_MODULE to the PS5 libc.prx runtime module")
    endif()
    if(NOT openlf2_icons OR NOT EXISTS "${openlf2_icons}/hicolor/512x512/apps/io.github.openlf2.OpenLF2.png")
        message(FATAL_ERROR "PS5 app staging needs the generated OpenLF2 512x512 PNG icon")
    endif()
    set(openlf2_ps5_elf "${CMAKE_CURRENT_BINARY_DIR}/eboot.elf")
    set(openlf2_ps5_fself "${CMAKE_CURRENT_BINARY_DIR}/eboot.bin")
    set(openlf2_ps5_app "${CMAKE_CURRENT_BINARY_DIR}/app/${OPENLF2_PS5_TITLE_ID}")
    set(OPENLF2_PS5_CONTENT_ID "UP9000-${OPENLF2_PS5_TITLE_ID}_00-${OPENLF2_PS5_CONTENT_SUFFIX}")
    set(openlf2_ps5_param "${CMAKE_CURRENT_BINARY_DIR}/ps5-param.json")
    configure_file("${PROJECT_SOURCE_DIR}/dist/ps5/param.json.in" "${openlf2_ps5_param}" @ONLY)
    add_custom_command(OUTPUT "${openlf2_ps5_elf}"
        COMMAND "${OPENLF2_PS5_NATIVE_TOOL}" link
            --in "$<TARGET_FILE:openlf2>" --out "${openlf2_ps5_elf}"
            --stub-dir "${PS5_PAYLOAD_SDK}/target/lib"
            --module-sdk "${OPENLF2_PS5_MODULE_SDK}"
            --companion-sdk "${OPENLF2_PS5_COMPANION_SDK}"
            --file-name eboot.elf
        DEPENDS openlf2
        COMMENT "Converting OpenLF2 to a PS5 ELF module"
        VERBATIM)
    add_custom_command(OUTPUT "${openlf2_ps5_fself}"
        COMMAND "${OPENLF2_PS5_NATIVE_TOOL}" self --sign
            --in "${openlf2_ps5_elf}" --out "${openlf2_ps5_fself}"
            --magic "${OPENLF2_PS5_FSELF_MAGIC}"
        COMMAND "${OPENLF2_PS5_NATIVE_TOOL}" self --inspect --file "${openlf2_ps5_fself}"
        DEPENDS "${openlf2_ps5_elf}"
        COMMENT "Signing and verifying the development FSELF"
        VERBATIM)
    add_custom_command(OUTPUT "${openlf2_ps5_app}/eboot.bin"
            "${openlf2_ps5_app}/sce_sys/param.json"
            "${openlf2_ps5_app}/sce_sys/icon0.png"
            "${openlf2_ps5_app}/sce_module/libc.prx"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${openlf2_ps5_app}/sce_sys"
            "${openlf2_ps5_app}/sce_module"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${openlf2_ps5_fself}" "${openlf2_ps5_app}/eboot.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${openlf2_ps5_param}" "${openlf2_ps5_app}/sce_sys/param.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${openlf2_icons}/hicolor/512x512/apps/io.github.openlf2.OpenLF2.png"
            "${openlf2_ps5_app}/sce_sys/icon0.png"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${OPENLF2_PS5_RUNTIME_MODULE}" "${openlf2_ps5_app}/sce_module/libc.prx"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/scripts" "${openlf2_ps5_app}/scripts"
        DEPENDS "${openlf2_ps5_fself}" "${openlf2_ps5_param}"
            "${OPENLF2_PS5_RUNTIME_MODULE}"
            "${openlf2_icons}/hicolor/512x512/apps/io.github.openlf2.OpenLF2.png"
        COMMENT "Staging the PS5 app folder"
        VERBATIM)
    add_custom_target(openlf2_ps5_image ALL DEPENDS
        "${openlf2_ps5_app}/eboot.bin" "${openlf2_ps5_app}/sce_sys/param.json"
        "${openlf2_ps5_app}/sce_sys/icon0.png" "${openlf2_ps5_app}/sce_module/libc.prx")
else()
    message(STATUS "Set OPENLF2_PS5_NATIVE_TOOL to enable the openlf2_ps5_image target")
endif()
