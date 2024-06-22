################################
# TIC-80 app (PSP)
################################

if (PSP)
    set(TIC80_SRC ${TIC80_SRC}
        ${CMAKE_SOURCE_DIR}/src/system/psp/main.c
    )

    add_executable(tic80_psp ${TIC80_SRC})

    target_include_directories(tic80_psp PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    message(${TIC80_SRC})

    target_compile_definitions(tic80_psp PRIVATE PSP)

    target_link_libraries(tic80_psp PRIVATE
        pspgum
        pspgu
        pspge
        pspaudio
        pspaudiolib
        pspvram
        pspdisplay
        pspdebug
        pspctrl
        psppower

        pspprof

        tic80studio
        png

        m
    )

    # Create an EBOOT.PBP file
    create_pbp_file(
        TARGET tic80_psp
        ICON_PATH NULL
        BACKGROUND_PATH NULL
        PREVIEW_PATH NULL
        TITLE TIC80
        VERSION ${VERSION_MAJOR}.${VERSION_MINOR}
    )
endif()
