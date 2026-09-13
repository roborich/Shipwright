cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)

project(soh LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

set (BUILD_UTILS OFF CACHE STRING "no utilities")
set (BUILD_SHARED_LIBS OFF CACHE STRING "install/link shared instead of static libs")

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# Global configuration types
################################################################################
set(CMAKE_CONFIGURATION_TYPES
    "Debug"
    "Release"
    CACHE STRING "" FORCE
)

################################################################################
# Global compiler options
################################################################################
if(MSVC)
    # remove default flags provided with CMake for MSVC
    set(CMAKE_C_FLAGS "")
    set(CMAKE_C_FLAGS_DEBUG "")
    set(CMAKE_C_FLAGS_RELEASE "")
    set(CMAKE_CXX_FLAGS "")
    set(CMAKE_CXX_FLAGS_DEBUG "")
    set(CMAKE_CXX_FLAGS_RELEASE "")
endif()

################################################################################
# Global linker options
################################################################################
if(MSVC)
    # remove default flags provided with CMake for MSVC
    set(CMAKE_EXE_LINKER_FLAGS "")
    set(CMAKE_MODULE_LINKER_FLAGS "")
    set(CMAKE_SHARED_LINKER_FLAGS "")
    set(CMAKE_STATIC_LINKER_FLAGS "")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "${CMAKE_STATIC_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS}")
endif()

################################################################################
# Common utils
################################################################################
include(CMake/Utils.cmake)

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Sub-projects
################################################################################
if (NOT TARGET libultraship)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libultraship ${CMAKE_BINARY_DIR}/libultraship)
endif()

# SOH [WASM] ZAPD is a host tool; a wasm build consumes pre-built .o2r archives.
if (NOT TARGET ZAPDLib AND NOT EMSCRIPTEN)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ZAPDTR/ZAPD ${CMAKE_BINARY_DIR}/ZAPD)
endif()

set(PROJECT_NAME soh)

################################################################################
# Sources
################################################################################
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/src/boot/build.c.in ${CMAKE_CURRENT_SOURCE_DIR}/src/boot/build.c @ONLY)
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/properties.h.in ${CMAKE_CURRENT_SOURCE_DIR}/properties.h @ONLY)

set(Header_Files "resource.h")
source_group("headers" FILES ${Header_Files})

# include {{{
file(GLOB Header_Files__include "include/*.h" "include/*.inc")
list(APPEND Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/libc/stdarg.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/bgm.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/math_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stdbool_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stddef_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stdlib_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/ultra64.h)
source_group("include" FILES ${Header_Files__include})
# }}}

# soh (root)
file(GLOB_RECURSE soh__ RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "soh/*.c" "soh/*.cpp" "soh/*.h" "soh/*.hpp")

# Add specific files that don't match the pattern
list(APPEND soh__ ${CMAKE_CURRENT_SOURCE_DIR}/soh/Enhancements/savestates_extern.inc)
list(APPEND soh__ ${CMAKE_CURRENT_SOURCE_DIR}/soh/Enhancements/speechsynthesizer/DarwinSpeechSynthesizer.mm)

# Create source groups that match the real file directory paths
foreach(source IN LISTS soh__)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_adjusted "${source_path}")
    source_group("${source_path_adjusted}" FILES "${source}")
endforeach()

# Fix UTF-8 issues for MSVC
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_source_files_properties(soh/OTRGlobals.cpp PROPERTIES COMPILE_FLAGS "/utf-8")
    set_source_files_properties(soh/Enhancements/tts/tts.cpp PROPERTIES COMPILE_FLAGS "/utf-8")
    set_source_files_properties(soh/Enhancements/custom-message/CustomMessageManager.cpp PROPERTIES COMPILE_FLAGS "/utf-8")
    set_source_files_properties(soh/Enhancements/custom-message/CustomMessageManager.h PROPERTIES COMPILE_FLAGS "/utf-8")
endif()

# handle Network removals
if (!BUILD_REMOTE_CONTROL)
    list(FILTER soh__ EXCLUDE REGEX "soh/Enhancements/crowd-control/")
endif()

# handle speechsynthesizer removals
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(FILTER soh__ EXCLUDE REGEX "soh/Enhancements/speechsynthesizer/Darwin")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(FILTER soh__ EXCLUDE REGEX "soh/Enhancements/speechsynthesizer/SAPI")
else()
    list(FILTER soh__ EXCLUDE REGEX "soh/Enhancements/speechsynthesizer/(Darwin|SAPI)")
endif()

find_library(ESPEAK espeak-ng)
if (NOT ESPEAK)
    list(FILTER soh__ EXCLUDE REGEX "soh/Enhancements/speechsynthesizer/ESpeak")
endif()

# soh/Extractor {{{
list(FILTER ship__ EXCLUDE REGEX "soh/Extractor/*")

# SOH [WASM] The ROM extractor links against ZAPD, which is not in the cross build. The
# filter above targets `ship__`, a variable that does not exist here, so the Extractor
# sources still arrive through the recursive `soh__` glob -- exclude them explicitly.
if(EMSCRIPTEN)
    list(FILTER soh__ EXCLUDE REGEX "soh/Extractor/")
endif()

# SOH [WASM] Emscripten joins the consoles in taking headers only: the ROM extractor
# links against ZAPD, which is not part of the cross build.
if(NOT CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch|CafeOS" AND NOT EMSCRIPTEN)
    file(GLOB_RECURSE soh__Extractor RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "soh/Extractor/*.c"
        "soh/Extractor/*.cpp"
        "soh/Extractor/*.h"
        "soh/Extractor/*.hpp"
    )
else()
    file(GLOB_RECURSE soh__Extractor RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "soh/Extractor/*.h"
        "soh/Extractor/*.hpp"
    )
endif()
# }}}

# src (decomp) {{{
file(GLOB_RECURSE src__ RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.c" "src/*.h")
set_source_files_properties(${src__} PROPERTIES COMPILE_OPTIONS "${WARNING_OVERRIDE}")

list(APPEND src__ ${CMAKE_CURRENT_SOURCE_DIR}/Resource.rc)
list(FILTER src__ EXCLUDE REGEX "src/dmadata/")
list(FILTER src__ EXCLUDE REGEX "src/elf_message/")
list(FILTER src__ EXCLUDE REGEX "src/libultra/io/")
list(FILTER src__ EXCLUDE REGEX "src/libultra/libc/")
list(FILTER src__ EXCLUDE REGEX "src/libultra/os/")
list(FILTER src__ EXCLUDE REGEX "src/libultra/rmon/")
list(APPEND src__ "src/libultra/libc/sprintf.c")
list(REMOVE_ITEM src__ "src/libultra/gu/cosf.c")
list(REMOVE_ITEM src__ "src/libultra/gu/lookat.c")
list(REMOVE_ITEM src__ "src/libultra/gu/lookathil.c")
list(REMOVE_ITEM src__ "src/libultra/gu/perspective.c")
list(REMOVE_ITEM src__ "src/libultra/gu/position.c")
list(REMOVE_ITEM src__ "src/libultra/gu/sinf.c")
list(REMOVE_ITEM src__ "src/libultra/gu/sinf.c")
list(REMOVE_ITEM src__ "src/libultra/gu/sqrtf.c")
list(REMOVE_ITEM src__ "src/libultra/gu/us2dex.c")

source_group("src" REGULAR_EXPRESSION "src/*")
source_group("src\\boot" REGULAR_EXPRESSION "src/boot/*")
source_group("src\\buffers" REGULAR_EXPRESSION "src/buffers/*")
source_group("src\\code" REGULAR_EXPRESSION "src/code/*")
source_group("src\\libultra" REGULAR_EXPRESSION "src/libultra/*")
source_group("src\\overlays\\actors" REGULAR_EXPRESSION "src/overlays/actors/*")
source_group("src\\overlays\\effects" REGULAR_EXPRESSION "src/overlays/effects/*")
source_group("src\\overlays\\gamestates" REGULAR_EXPRESSION  "src/overlays/gamestates/*")
source_group("src\\overlays\\misc" REGULAR_EXPRESSION  "src/overlays/misc/*")
# }}}

set(ALL_FILES
    ${Header_Files}
    ${Header_Files__include}
    ${soh__}
    ${soh__Extractor}
    ${src__}
)

################################################################################
# Target
################################################################################
add_executable(${PROJECT_NAME} ${ALL_FILES})

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
endif()

set(ROOT_NAMESPACE soh)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_GLOBAL_KEYWORD "Win32Proj"
    )
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE "TRUE"
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE "TRUE"
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        OUTPUT_NAME "soh-macos"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "soh.elf"
    )
endif()
################################################################################
# MSVC runtime library
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    get_property(MSVC_RUNTIME_LIBRARY_DEFAULT TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY)
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
            $<$<CONFIG:Debug>:
                MultiThreadedDebug
            >
            $<$<CONFIG:Release>:
                MultiThreaded
            >
            $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
            $<$<CONFIG:Debug>:
                MultiThreadedDebug
            >
            $<$<CONFIG:Release>:
                MultiThreaded
            >
            $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
        )
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR})
endif()
################################################################################
# Find/download Dr Libs (For custom audio)
################################################################################
include(FetchContent)

FetchContent_Declare(
        dr_libs
        GIT_REPOSITORY https://github.com/mackron/dr_libs.git
        GIT_TAG da35f9d6c7374a95353fd1df1d394d44ab66cf01
)
FetchContent_MakeAvailable(dr_libs)

find_package(SDL2)
set(SDL2-INCLUDE ${SDL2_INCLUDE_DIRS})

if (BUILD_REMOTE_CONTROL)
    find_package(SDL2_net)

    if(NOT SDL2_net_FOUND)
        message(STATUS "SDL2_net not found (it's possible the version installed is too old). Disabling BUILD_REMOTE_CONTROL.")
        set(BUILD_REMOTE_CONTROL 0)
    else()
        set(SDL2-NET-INCLUDE ${SDL_NET_INCLUDE_DIRS})
    endif()
endif()

if (ESPEAK)
    add_compile_definitions(ESPEAK=1)
endif()

################################################################################
# Compile definitions
################################################################################

target_include_directories(${PROJECT_NAME} PRIVATE assets
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${CMAKE_CURRENT_SOURCE_DIR}/../libultraship/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ZAPDTR/ZAPD/resource/type
    ${SDL2-INCLUDE}
    ${SDL2-NET-INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/
    ${dr_libs_SOURCE_DIR}
    .
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            "$<$<CONFIG:Debug>:"
                "_DEBUG;"
                "_CRT_SECURE_NO_WARNINGS;"
                "ENABLE_DX11;"
            ">"
            "$<$<CONFIG:Release>:"
                "NDEBUG;"
            ">"
            "$<$<BOOL:${BUILD_REMOTE_CONTROL}>:ENABLE_REMOTE_CONTROL>"
            "INCLUDE_GAME_PRINTF;"
            "F3DEX_GBI_2"
            "UNICODE;"
            "_UNICODE"
            SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
            LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}
            STORMLIB_NO_AUTO_LINK
            "_CRT_SECURE_NO_WARNINGS;"
            NOMINMAX
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            "$<$<CONFIG:Debug>:"
                "NOINCLUDE_GAME_PRINTF;"
                "_DEBUG;"
                "_CRT_SECURE_NO_WARNINGS;"
                "ENABLE_OPENGL;"
            ">"
            "$<$<CONFIG:Release>:"
                "NDEBUG;"
            ">"
            "INCLUDE_GAME_PRINTF;"
            "F3DEX_GBI_2"
            "WIN32;"
            "UNICODE;"
            "_UNICODE;"
            SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
            LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}
            STORMLIB_NO_AUTO_LINK
            NOMINMAX
        )
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
            "_DEBUG;"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG;"
        ">"
        "F3DEX_GBI_2;"
        "SPDLOG_NO_THREAD_ID;"
        "SPDLOG_NO_TLS;"
        "STBI_NO_THREAD_LOCALS;"
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}
    )
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|Clang|AppleClang")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
            "_DEBUG;"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG;"
        ">"
        "F3DEX_GBI_2;"
        "$<$<BOOL:${BUILD_REMOTE_CONTROL}>:ENABLE_REMOTE_CONTROL>;"
        "_CONSOLE;"
        "_CRT_SECURE_NO_WARNINGS;"
        "ENABLE_OPENGL;"
        "UNICODE;"
        "_UNICODE;"
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}
    )
endif()
################################################################################
# Compile and link options
################################################################################
if(MSVC)
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /w;
                /Od
            >
            $<$<CONFIG:Release>:
                /Oi;
                /Gy;
                /W3
            >
            /bigobj;
            /sdl-;
            /permissive-;
            /MP;
            ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
            ${DEFAULT_CXX_EXCEPTION_HANDLING}
        )
        target_compile_options(${PROJECT_NAME} PRIVATE  $<$<CONFIG:Debug>:/ZI;>)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /RTCs
            >
            $<$<CONFIG:Release>:
                /O2;
                /Oi;
                /Gy
            >
            /permissive-;
            /MP;
            /sdl-;
            /w;
            ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
            ${DEFAULT_CXX_EXCEPTION_HANDLING}
        )
    endif()
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /INCREMENTAL
            >
            $<$<CONFIG:Release>:
                /OPT:REF;
                /OPT:ICF;
                /INCREMENTAL:NO;
                /FORCE:MULTIPLE
            >
            /MANIFEST:NO;
            /DEBUG;
            /SUBSYSTEM:WINDOWS
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /STACK:8777216
            >
            $<$<CONFIG:Release>:
                /OPT:REF;
                /OPT:ICF;
                /INCREMENTAL:NO;
                /FORCE:MULTIPLE
            >
            /MANIFEST:NO;
            /DEBUG;
            /SUBSYSTEM:WINDOWS
        )
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall -Wextra -Wno-error
            -Wformat-security
            -Wno-return-type
            -Wno-unused-parameter
            -Wno-unused-function
            -Wno-unused-variable
            -Wno-missing-field-initializers
            -Wno-parentheses
            -Wno-narrowing
            -Wno-missing-braces
            $<$<COMPILE_LANGUAGE:C>:
                -Werror-implicit-function-declaration
                -Wno-incompatible-pointer-types
                -Wno-int-conversion
            >
            $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
            $<$<COMPILE_LANGUAGE:CXX>:
                -Wno-c++11-narrowing
                -Wno-deprecated-enum-enum-conversion
            >
            -pthread
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            -pthread
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall -Wextra -Wno-error
            -Wformat-security
            -Wno-return-type
            -Wno-unused-parameter
            -Wno-unused-function
            -Wno-unused-variable
            -Wno-missing-field-initializers
            -Wno-parentheses
            -Wno-narrowing
            -Wno-missing-braces
            $<$<COMPILE_LANGUAGE:C>:
                -Werror-implicit-function-declaration
                -Wno-incompatible-pointer-types
            >
            $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
            $<$<COMPILE_LANGUAGE:CXX>:
                -Wno-c++11-narrowing
                -Wno-deprecated-enum-enum-conversion
            >
            -pthread
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            -pthread
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O2

            # disable some warnings to not clutter output
            -Wno-multichar
            -Wno-return-type
            -Wno-narrowing
            -Wno-switch-outside-range
            $<$<COMPILE_LANGUAGE:C>:
                -Werror-implicit-function-declaration
                -Wno-incompatible-pointer-types
                -Wno-discarded-array-qualifiers
                -Wno-discarded-qualifiers
                -Wno-int-conversion
                -Wno-builtin-declaration-mismatch
                -Wno-switch-unreachable
                -Wno-stringop-overflow
            >
        )
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(CPU_OPTION -msse2 -mfpmath=sse)
        endif()

        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall -Wextra -Wno-error
            -Wformat-security
            -Wno-unused-parameter
            -Wno-unused-function
            -Wno-unused-variable
            -Wno-missing-field-initializers
            -Wno-parentheses
            -Wno-narrowing
            -Wno-missing-braces
            $<$<COMPILE_LANGUAGE:C>:
                -Werror-implicit-function-declaration
                -Wno-implicit-int
                -Wno-incompatible-pointer-types
                -Wno-int-conversion
            >
            $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
            # SOH [WASM] -pthread under Emscripten requests a shared-memory build, which
            # needs SharedArrayBuffer and COOP/COEP. The wasm target is single-threaded.
            $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:-pthread>
        ${CPU_OPTION}
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:-pthread>
                #-fsanitize=address
            -Wl,-export-dynamic
        )
    endif()
endif()
################################################################################
# Pre build events
################################################################################
if (CMAKE_GENERATOR MATCHES "Visual Studio")
    add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/soh/assets/extractor ${CMAKE_BINARY_DIR}/soh/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/soh/assets/xml ${CMAKE_BINARY_DIR}/soh/assets/xml
    )
endif()
if(NOT CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch|CafeOS")
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMENT "Copying asset xmls..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/soh/assets/extractor $<TARGET_FILE_DIR:soh>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/soh/assets/xml $<TARGET_FILE_DIR:soh>/assets/xml
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:soh>/assets/symbols
    #    COMMAND ${VS_COPY_ASSETS_CMD}
    )
endif()
################################################################################
# Dependencies
################################################################################
add_dependencies(${PROJECT_NAME}
    libultraship
)
# SOH [WASM] Emscripten joins the consoles in not building ZAPD.
if(NOT CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch|CafeOS" AND NOT EMSCRIPTEN)
add_dependencies(${PROJECT_NAME}
    ZAPDLib
)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(glfw3 REQUIRED)
    find_package(Ogg CONFIG REQUIRED)
    link_libraries(Ogg::ogg)

    find_package(Vorbis CONFIG REQUIRED)
    link_libraries(Vorbis::vorbisfile)
    find_package(Opus CONFIG REQUIRED)
    link_libraries(Opus::opus)
    find_package(OpusFile CONFIG REQUIRED)
    link_libraries(OpusFile::opusfile CONFIG REQUIRED)
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        set(ADDITIONAL_LIBRARY_DEPENDENCIES
            "libultraship;"
            "ZAPDLib;"
            "glu32;"
            "SDL2::SDL2;"
            "SDL2::SDL2main;"
            "$<$<BOOL:${BUILD_REMOTE_CONTROL}>:SDL2_net::SDL2_net-static>"
            "glfw;"
            "winmm;"
            "imm32;"
            "version;"
            "setupapi"
            "Ogg::ogg"
            "Opus::opus"
            "Vorbis::vorbis"
            "Vorbis::vorbisenc"
            "Vorbis::vorbisfile"
            "OpusFile::opusfile"
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        set(ADDITIONAL_LIBRARY_DEPENDENCIES
            "libultraship;"
            "ZAPDLib;"
            "glu32;"
            "SDL2::SDL2;"
            "SDL2::SDL2main;"
            "glfw;"
            "winmm;"
            "imm32;"
            "version;"
            "setupapi"
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    find_package(SDL2)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    set(ADDITIONAL_LIBRARY_DEPENDENCIES
        "libultraship;"
        SDL2::SDL2
        -lglad
        Threads::Threads
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
    find_package(SDL2 REQUIRED)
    set(ADDITIONAL_LIBRARY_DEPENDENCIES
        "libultraship;"
        SDL2::SDL2-static

        "$<$<CONFIG:Debug>:-Wl,--wrap=abort>"
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${DEVKITPRO}/portlibs/wiiu/include/
    )
elseif(EMSCRIPTEN)
    # SOH [WASM] SDL2, Ogg and Vorbis come from Emscripten ports (requested globally in
    # the root CMakeLists), so there is nothing to find_package here and no ZAPDLib.
    # Opus and OpusFile have no Emscripten port; the two functions that use them are
    # stubbed in soh/mixer.c, which costs custom streamed Opus audio and nothing else.
    set(ADDITIONAL_LIBRARY_DEPENDENCIES
        "libultraship;"
    )
else()
    find_package(SDL2)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    find_package(Ogg REQUIRED)
    find_package(Vorbis REQUIRED)
    find_package(Opus REQUIRED)
    find_package(OpusFile REQUIRED)
     set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "libultraship;"
        "ZAPDLib;"
        SDL2::SDL2
        "Ogg::ogg"
        "Vorbis::vorbis"
        "Vorbis::vorbisenc"
        "Vorbis::vorbisfile"
        "Opus::opus"
        "Opusfile::Opusfile"
        "$<$<BOOL:${BUILD_REMOTE_CONTROL}>:SDL2_net::SDL2_net>"
        ${CMAKE_DL_LIBS}
        Threads::Threads
    )
endif()

if(NOT CMAKE_SYSTEM_NAME MATCHES "Darwin|NintendoSwitch|CafeOS")
INSTALL(TARGETS soh DESTINATION . COMPONENT ship)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
INSTALL(FILES $<TARGET_PDB_FILE:soh> DESTINATION ./debug COMPONENT ship)
INSTALL(FILES ${CMAKE_BINARY_DIR}/soh/soh.o2r DESTINATION . COMPONENT ship)
endif()

find_program(CURL NAMES curl DOC "Path to the curl program.  Used to download files.")
execute_process(COMMAND ${CURL} -sSfL https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/master/gamecontrollerdb.txt -o ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt OUTPUT_VARIABLE RESULT)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/macosx/Info.plist.in ${CMAKE_BINARY_DIR}/macosx/Info.plist @ONLY)
INSTALL(TARGETS soh DESTINATION ../MacOS COMPONENT ship)
INSTALL(FILES ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt DESTINATION ../MacOS COMPONENT ship)
INSTALL(FILES ${CMAKE_BINARY_DIR}/soh/soh.o2r DESTINATION ../Resources COMPONENT ship)
elseif(NOT "${CMAKE_SYSTEM_NAME}" MATCHES "NintendoSwitch|CafeOS")
INSTALL(FILES ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt DESTINATION . COMPONENT ship)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch|CafeOS")
    if (NOT TARGET pathconf)
        add_library(pathconf OBJECT platform/pathconf.c)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}" $<TARGET_OBJECTS:pathconf> )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch")

nx_generate_nacp(Ship.nacp
   NAME "Ship of Harkinian"
   AUTHOR "${PROJECT_TEAM}"
   VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

nx_create_nro(soh
    NACP Ship.nacp
    ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg
)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/soh.nro DESTINATION . COMPONENT ship)

elseif(CMAKE_SYSTEM_NAME MATCHES "CafeOS")

wut_create_rpx(${PROJECT_NAME})

wut_create_wuhb(${PROJECT_NAME}
    NAME       "Ship of Harkinian"
    SHORTNAME  "SoH"
    AUTHOR     "${PROJECT_TEAM}"
    ICON       ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg
)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/soh.rpx ${CMAKE_CURRENT_BINARY_DIR}/soh.wuhb DESTINATION . COMPONENT ship)

endif()

################################################################################
# SOH [WASM] Emscripten packaging
################################################################################
if(EMSCRIPTEN)
    # Where the pre-built .o2r archives live. They are produced by a desktop build
    # (ZAPD is a host tool), then baked into the module's virtual filesystem at the
    # paths LocateFileAcrossAppDirs searches. Override to point at your own copies.
    set(SOH_WASM_ASSET_DIR "${CMAKE_SOURCE_DIR}/build-cmake/soh" CACHE PATH
        "Directory holding pre-built soh.o2r / oot.o2r for the wasm build")

    option(SOH_WASM_GUI_ONLY "Boot only the window, WebGL2 and ImGui -- no game, no oot.o2r" OFF)

    # When ON, oot.o2r / config / saves are NOT baked into the module. The host page
    # supplies them as bytes instead (Module.shipFiles; see soh/wasm/index.html), which is
    # how an embedder such as Prelude of Light hands over an archive it already holds
    # without a second copy or a download. soh.o2r stays baked in either way: it is built
    # from this repo and version-checked against gBuildVersion, so letting a host supply it
    # only invites version-mismatch bugs.
    option(SOH_WASM_HOST_FILES "Take oot.o2r, config and saves from the host page at boot" OFF)

    if(NOT EXISTS "${SOH_WASM_ASSET_DIR}/soh.o2r")
        message(FATAL_ERROR
            "soh.o2r not found in ${SOH_WASM_ASSET_DIR}. Build it from a desktop build "
            "(`make GenerateSohOtr`) and point -DSOH_WASM_ASSET_DIR at it.")
    endif()

    # SHELL: keeps each --preload-file and its path together as one unit. Without it CMake
    # de-duplicates the repeated `--preload-file` token and silently drops all but the first
    # archive.
    target_link_options(${PROJECT_NAME} PRIVATE
        "SHELL:--preload-file ${SOH_WASM_ASSET_DIR}/soh.o2r@/soh.o2r"
        -sFORCE_FILESYSTEM=1
        # The host page writes game files into MEMFS during preRun. HEAPU8 lets it read
        # the wasm heap size (memtrace); Emscripten keeps Module.HEAPU8 current across
        # memory growth only when it is exported.
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,cwrap,HEAPU8"
        # Keep function names in the wasm so a browser stack trace reads
        # Menu::Draw rather than $func11146. Costs a name section, nothing at runtime.
        --profiling-funcs
        # Binaryen inlines any function that has exactly one caller, whatever its size.
        # That folded the 35 RegionTable_Init_* area builders into one 3.3 MB
        # RegionTable_Init, and V8's optimising compiler died on it a few minutes into
        # every session (the check tracker runs it on each game load). Cap the size of
        # single-caller functions that get inlined; the biggest ones are table builders
        # that gain nothing from it.
        "SHELL:-sBINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=1000"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=536870912
        -sSTACK_SIZE=4194304
        -sEXIT_RUNTIME=0
    )

    # --preload-file inputs reach the link only as options, and CMake does not treat those as
    # dependencies: a regenerated soh.o2r would leave soh up to date and soh.data stale.
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${SOH_WASM_ASSET_DIR}/soh.o2r")

    # VFS paths packaged in soh.data, reported to the page through build-info.js below.
    set(_soh_wasm_baked_files "/soh.o2r")

    # A profile directory (config + saves) baked into the VFS. Under Emscripten
    # Context::GetAppDirectoryPath() falls through to ".", so SoH looks for
    # ./shipofharkinian.json and ./Save/*.sav at the filesystem root.
    #
    # This is a stopgap: MEMFS is in-memory, so anything the game writes is lost on
    # reload. Real persistence means IDBFS + syncfs, or handing these to Prelude along
    # with the archives. Files are staged into the build tree first because
    # --preload-file cannot cope with spaces in a source path.
    set(SOH_WASM_PROFILE_DIR "" CACHE PATH
        "Optional directory holding shipofharkinian.json and Save/ to bake into the wasm VFS")

    if(SOH_WASM_PROFILE_DIR AND NOT SOH_WASM_HOST_FILES AND EXISTS "${SOH_WASM_PROFILE_DIR}/shipofharkinian.json")
        set(_soh_wasm_profile "${CMAKE_CURRENT_BINARY_DIR}/wasm-profile")
        file(REMOVE_RECURSE "${_soh_wasm_profile}")
        file(MAKE_DIRECTORY "${_soh_wasm_profile}")
        file(COPY "${SOH_WASM_PROFILE_DIR}/shipofharkinian.json" DESTINATION "${_soh_wasm_profile}")
        if(EXISTS "${SOH_WASM_PROFILE_DIR}/Save")
            file(COPY "${SOH_WASM_PROFILE_DIR}/Save" DESTINATION "${_soh_wasm_profile}"
                 FILES_MATCHING PATTERN "*.sav")
        endif()
        # The copy above runs at configure time, so an edited profile needs a reconfigure (for
        # files that existed when it last ran) and a relink. A newly added save needs a manual
        # reconfigure.
        file(GLOB _soh_wasm_profile_sources "${SOH_WASM_PROFILE_DIR}/shipofharkinian.json"
             "${SOH_WASM_PROFILE_DIR}/Save/*.sav")
        file(GLOB_RECURSE _soh_wasm_profile_staged "${_soh_wasm_profile}/*")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_soh_wasm_profile_sources})
        set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS ${_soh_wasm_profile_staged})
        target_link_options(${PROJECT_NAME} PRIVATE
            "SHELL:--preload-file ${_soh_wasm_profile}/shipofharkinian.json@/shipofharkinian.json"
        )
        list(APPEND _soh_wasm_baked_files "/shipofharkinian.json")
        file(GLOB _soh_wasm_staged_saves RELATIVE "${_soh_wasm_profile}" "${_soh_wasm_profile}/Save/*.sav")
        list(TRANSFORM _soh_wasm_staged_saves PREPEND "/")
        list(APPEND _soh_wasm_baked_files ${_soh_wasm_staged_saves})
        if(EXISTS "${_soh_wasm_profile}/Save")
            target_link_options(${PROJECT_NAME} PRIVATE
                "SHELL:--preload-file ${_soh_wasm_profile}/Save@/Save"
            )
        endif()
        message(STATUS "WASM profile baked in from ${SOH_WASM_PROFILE_DIR}")
    endif()

    if(SOH_WASM_GUI_ONLY)
        target_compile_definitions(${PROJECT_NAME} PRIVATE SOH_WASM_GUI_ONLY)
    elseif(SOH_WASM_HOST_FILES)
        # Nothing to bake: the host page writes the game archive into the VFS at boot.
        message(STATUS "WASM: oot.o2r, config and saves come from the host page")
    else()
        # The game needs the asset archive too.
        if(NOT EXISTS "${SOH_WASM_ASSET_DIR}/oot.o2r")
            message(FATAL_ERROR
                "oot.o2r not found in ${SOH_WASM_ASSET_DIR}. Extract it with a desktop "
                "build, or configure with -DSOH_WASM_HOST_FILES=ON to have the page supply it.")
        endif()
        target_link_options(${PROJECT_NAME} PRIVATE
            "SHELL:--preload-file ${SOH_WASM_ASSET_DIR}/oot.o2r@/oot.o2r"
        )
        set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${SOH_WASM_ASSET_DIR}/oot.o2r")
        list(APPEND _soh_wasm_baked_files "/oot.o2r")
    endif()

    # Tells the page how this module was built: whether it must supply oot.o2r, and which
    # paths it must not write because soh.data already has them. See host-page.js.
    if(SOH_WASM_HOST_FILES)
        set(SOH_WASM_BUILD_INFO_HOST_FILES true)
    else()
        set(SOH_WASM_BUILD_INFO_HOST_FILES false)
    endif()
    list(JOIN _soh_wasm_baked_files "\", \"" _soh_wasm_baked_joined)
    set(SOH_WASM_BUILD_INFO_BAKED_FILES "\"${_soh_wasm_baked_joined}\"")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm/build-info.js.in
                   ${CMAKE_CURRENT_BINARY_DIR}/build-info.js @ONLY)
    target_link_options(${PROJECT_NAME} PRIVATE "SHELL:--pre-js ${CMAKE_CURRENT_BINARY_DIR}/build-info.js")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/build-info.js")

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm/host-page.js
                   ${CMAKE_CURRENT_BINARY_DIR}/host-page.js COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm/index.html
                   ${CMAKE_CURRENT_BINARY_DIR}/index.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm/host.html
                   ${CMAKE_CURRENT_BINARY_DIR}/host.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wasm/HOST-API.md
                   ${CMAKE_CURRENT_BINARY_DIR}/HOST-API.md COPYONLY)
endif()
