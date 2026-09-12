# SOH [WASM] Dependency provisioning for the Emscripten target.
#
# LUS and soh call find_package(REQUIRED) for libzip, nlohmann_json, tinyxml2, spdlog and
# the audio codecs. None of those resolve when cross-compiling to wasm: the host's
# Homebrew/apt copies are native binaries, and Emscripten ships ports for only some of
# them. Rather than patch every find_package call site, we declare each dependency with
# OVERRIDE_FIND_PACKAGE (CMake >= 3.24), which redirects find_package to a source build.
#
# Emscripten *does* ship ports for SDL2, ogg, vorbis and zlib; those are requested with
# --use-port on the compile/link line instead (see the EMSCRIPTEN branch in the root
# CMakeLists). Opus and OpusFile have no port and no source build here yet — see
# wasm-port.md.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- zlib -------------------------------------------------------------------
# libzip does find_package(ZLIB REQUIRED) and wants the ZLIB::ZLIB target, which upstream
# zlib's own CMakeLists does not define. The redirect "extra" file below is CMake's
# documented hook for filling that gap.
FetchContent_Declare(
    ZLIB
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    OVERRIDE_FIND_PACKAGE
)
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-extra.cmake"
"if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
set(ZLIB_INCLUDE_DIR \"\${zlib_SOURCE_DIR}\" \"\${zlib_BINARY_DIR}\" CACHE INTERNAL \"\")
set(ZLIB_INCLUDE_DIRS \${ZLIB_INCLUDE_DIR})
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
")

# --- libzip -----------------------------------------------------------------
# .o2r archives are zip containers, so this one is load-bearing. Everything optional is
# switched off: the compression backends beyond zlib, the CLI tools, tests and docs.
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_OSSFUZZ OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG v1.10.1
    OVERRIDE_FIND_PACKAGE
)

# --- nlohmann_json ----------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    OVERRIDE_FIND_PACKAGE
)

# --- tinyxml2 ---------------------------------------------------------------
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
    OVERRIDE_FIND_PACKAGE
)

# --- spdlog -----------------------------------------------------------------
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    OVERRIDE_FIND_PACKAGE
)
