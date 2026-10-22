cmake_minimum_required(VERSION 3.15)

# ==============================================================================
# devkitPro / devkitARM Toolchain Configuration
# ==============================================================================
if(NOT DEFINED ENV{DEVKITPRO})
    if(WIN32)
        set(DEVKITPRO "C:/devkitPro")
    else()
        set(DEVKITPRO "/opt/devkitpro")
    endif()
    message(STATUS "DEVKITPRO not set in environment. Using default: ${DEVKITPRO}")
else()
    set(DEVKITPRO "$ENV{DEVKITPRO}")
endif()

if(NOT DEFINED ENV{DEVKITARM})
    set(DEVKITARM "${DEVKITPRO}/devkitARM")
else()
    set(DEVKITARM "$ENV{DEVKITARM}")
endif()

# Check for standalone cross-compiler if not loaded through 3DS.cmake
if(NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_SYSTEM_NAME "Generic")
    set(CMAKE_SYSTEM_PROCESSOR "armv6k")
    
    set(CMAKE_C_COMPILER "${DEVKITARM}/bin/arm-none-eabi-gcc")
    set(CMAKE_CXX_COMPILER "${DEVKITARM}/bin/arm-none-eabi-g++")
    set(CMAKE_AR "${DEVKITARM}/bin/arm-none-eabi-gcc-ar" CACHE FILEPATH "Archiver")
    set(CMAKE_RANLIB "${DEVKITARM}/bin/arm-none-eabi-gcc-ranlib" CACHE FILEPATH "Ranlib")
endif()

project(PokeSurvivors3DS C CXX)

set(LIBCTRU "${DEVKITPRO}/libctru")
set(PORTLIBS "${DEVKITPRO}/portlibs/3ds")

# ==============================================================================
# Architecture & Performance Flags for ARM1176JZF-S (Nintendo 3DS)
# ==============================================================================
set(ARCH_FLAGS "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp")
set(OPTIMIZATION_FLAGS "-O3 -flto -ffast-math -fno-rtti -fno-exceptions -fomit-frame-pointer")
set(WARNING_FLAGS "-Wall -Wextra -Wno-unused-parameter")

set(CMAKE_C_FLAGS "${ARCH_FLAGS} ${OPTIMIZATION_FLAGS} ${WARNING_FLAGS} -std=c11" CACHE STRING "C Flags" FORCE)
set(CMAKE_CXX_FLAGS "${ARCH_FLAGS} ${OPTIMIZATION_FLAGS} ${WARNING_FLAGS} -std=c++20" CACHE STRING "CXX Flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-specs=3dsx.specs ${ARCH_FLAGS} ${OPTIMIZATION_FLAGS} -Wl,--gc-sections" CACHE STRING "Linker Flags" FORCE)

# ==============================================================================
# Include Directories & Libraries
# ==============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBCTRU}/include
    ${DEVKITPRO}/citro2d/include
    ${DEVKITPRO}/citro3d/include
    ${PORTLIBS}/include
)

link_directories(
    ${LIBCTRU}/lib
    ${PORTLIBS}/lib
)

# Source files
file(GLOB_RECURSE SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_executable(PokeSurvivors3DS.elf ${SOURCES})

# Required libraries: Citro2D, Citro3D, libctru, math
target_link_libraries(PokeSurvivors3DS.elf
    citro2d
    citro3d
    ctru
    m
)

# ==============================================================================
# tex3ds Texture Conversion Rules (PNG -> .t3x PICA200 Swizzled Binary)
# ==============================================================================
find_program(TOOL_TEX3DS tex3ds HINTS "${DEVKITARM}/bin")

set(ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${ROMFS_DIR}/gfx")
file(MAKE_DIRECTORY "${ROMFS_DIR}/audio")

# Copy baked 16-bit PCM WAV assets to RomFS if present in assets/audio
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/" DESTINATION "${ROMFS_DIR}/audio")
endif()

# Macro to invoke tex3ds and compile spritesheets into hardware-swizzled .t3x files
# Supports formats: rgba8888, rgba4444, rgba5551, rgb565, etc1, etc1a4
function(add_t3x_spritesheet TARGET_NAME OUTPUT_T3X FORMAT)
    set(INPUT_IMAGES ${ARGN})
    add_custom_command(
        OUTPUT "${OUTPUT_T3X}"
        COMMAND ${TOOL_TEX3DS} -o "${OUTPUT_T3X}" -f ${FORMAT} -z auto "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Idle.png"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Idle.png"
        COMMENT "Compiling single-sprite texture ${OUTPUT_T3X} via tex3ds (${FORMAT})"
        VERBATIM
    )
    add_custom_target(${TARGET_NAME} DEPENDS "${OUTPUT_T3X}")
    add_dependencies(PokeSurvivors3DS.elf ${TARGET_NAME})
endfunction()

# Example texture atlas rules (Max 1024x1024 dimension for PICA200 GPU)
set(PIKACHU_PNGS
    "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Idle.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Walk.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Attack.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Hurt.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Faint.png"
)

if(TOOL_TEX3DS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/pokemon/25/Idle.png")
    add_t3x_spritesheet(sheet_player "${ROMFS_DIR}/gfx/sheet_player.t3x" "rgba4444" ${PIKACHU_PNGS})
endif()

# ==============================================================================
# 3DS Homebrew Packaging (.3dsx and .smdh)
# ==============================================================================
find_program(TOOL_3DSXTOOL 3dsxtool HINTS "${DEVKITPRO}/tools/bin" "${DEVKITARM}/bin")
find_program(TOOL_SMDHTOOL smdhtool HINTS "${DEVKITPRO}/tools/bin" "${DEVKITARM}/bin")
find_program(TOOL_MAKEROM makerom HINTS "${DEVKITPRO}/tools/bin" "${DEVKITARM}/bin" "${CMAKE_CURRENT_SOURCE_DIR}/tools/local/makerom")

set(APP_TITLE "PokeSurvivors 3DS")
set(APP_AUTHOR "PokeSurvivors Native Team")
set(APP_DESC "Native C++ Port for Nintendo 3DS (Old/New 3DS)")
set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/pokesurvivor-main/assets/icons/icon-48.png")

if(TOOL_SMDHTOOL)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.smdh
        COMMAND ${TOOL_SMDHTOOL} --create "${APP_TITLE}" "${APP_DESC}" "${APP_AUTHOR}" "${APP_ICON}" ${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.smdh
        DEPENDS ${APP_ICON}
        COMMENT "Generating SMDH metadata and icon"
        VERBATIM
    )
    add_custom_target(app_smdh DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.smdh")
endif()

if(TOOL_3DSXTOOL AND TOOL_SMDHTOOL)
    add_custom_target(PokeSurvivors3DS.3dsx ALL
        COMMAND ${TOOL_3DSXTOOL} $<TARGET_FILE:PokeSurvivors3DS.elf> ${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.3dsx --smdh=${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.smdh --romfs=${ROMFS_DIR}
        DEPENDS PokeSurvivors3DS.elf app_smdh
        COMMENT "Building .3dsx Homebrew binary with RomFS filesystem"
        VERBATIM
    )
endif()

if(TOOL_MAKEROM AND TOOL_SMDHTOOL)
    add_custom_target(cia ALL
        COMMAND "${TOOL_MAKEROM}" -f cia
            -o "${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.cia"
            -elf $<TARGET_FILE:PokeSurvivors3DS.elf>
            -rsf "${CMAKE_CURRENT_SOURCE_DIR}/build_rules/app.rsf"
            -icon "${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.smdh"
            -target t -exefslogo
            "-DROMFS_DIR=${ROMFS_DIR}"
        DEPENDS PokeSurvivors3DS.elf app_smdh
            "${CMAKE_CURRENT_SOURCE_DIR}/build_rules/app.rsf"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/PokeSurvivors3DS.cia"
        COMMENT "Generating .cia installable package for FBI"
        VERBATIM
    )
else()
    message(WARNING "CIA target unavailable: install makerom and smdhtool, then rerun CMake.")
endif()
