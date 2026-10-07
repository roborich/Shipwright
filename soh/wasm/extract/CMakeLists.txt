# SOH [WASM] ROM -> o2r converter as its own Emscripten module.
#
# The desktop game extracts in-process: Extract.cpp validates the ROM, then hands the bytes
# to SohTorch::Extract (TorchExtract.cpp), which runs Torch over the version's yml recipe and
# writes oot.o2r. In a browser that job belongs in a worker, separate from the game, so this
# target builds Torch + TorchExtract.cpp + the pure ROM checks into `soh-extract.{js,wasm}`
# with one exported function. Background, measurements and the host contract:
# wasm-rom-extract.md, soh/wasm/HOST-API.md.
#
# The root CMakeLists configures the torch library for this (BUILD_OOT only,
# PORT_VERSION_ENDIANNESS=ON, as the desktop game has it). Torch's own Emscripten block only
# sets CMAKE_EXE_LINKER_FLAGS in its directory, for its standalone CLI; this module is linked
# here, so it gets the tree's -fwasm-exceptions ABI and none of Torch's JS-exception flags.
# Nothing here links libultraship: Torch writes the archive with its bundled miniz, so libzip,
# libpng, StormLib and the window/GL/audio code all stay out.

set(SOH_EXTRACT_NAME soh-extract)
set(SOH_EXTRACTOR_DIR ${CMAKE_SOURCE_DIR}/soh/soh/Extractor)

add_executable(${SOH_EXTRACT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/RomExtractor.cpp
    ${SOH_EXTRACTOR_DIR}/TorchExtract.cpp
    ${SOH_EXTRACTOR_DIR}/RomInfo.cpp
    ${SOH_EXTRACTOR_DIR}/FastCrc32C.c
)

set_target_properties(${SOH_EXTRACT_NAME} PROPERTIES
    CXX_STANDARD 20
    C_STANDARD 11
    OUTPUT_NAME ${SOH_EXTRACT_NAME}
    SUFFIX ".js"
    # Next to soh.js, so "the wasm artifacts" a host copies are one directory.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/soh
)

target_include_directories(${SOH_EXTRACT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/soh
    ${SOH_EXTRACTOR_DIR}
)

target_compile_definitions(${SOH_EXTRACT_NAME} PRIVATE
    # Written into the archive as `portVersion`; the game refuses an archive from another
    # port version, so this must be the version of the soh.wasm it feeds. The game compares
    # against soh/src/boot/build.c, generated from the same project() version.
    SOH_EXTRACT_PORT_VERSION="${CMAKE_PROJECT_VERSION}"
)

# TorchExtract.cpp includes Companion.h, whose headers come from torch's own directories.
target_link_libraries(${SOH_EXTRACT_NAME} PRIVATE torch)
get_target_property(TORCH_SOURCE_DIR torch SOURCE_DIR)
target_include_directories(${SOH_EXTRACT_NAME} PRIVATE
    ${TORCH_SOURCE_DIR}
    ${TORCH_SOURCE_DIR}/src
    ${TORCH_SOURCE_DIR}/lib
)

set(SOH_EXTRACT_RECIPE_DIR ${CMAKE_SOURCE_DIR}/soh/assets/yml)

target_link_options(${SOH_EXTRACT_NAME} PRIVATE
    # The extraction recipe: config.yml (ROM hash -> version directory and archive name) and
    # one directory of asset ymls per ROM version. All 14 versions ship, so the module can
    # detect the version itself. (Quoted as SHELL: so CMake keeps flag and path together.)
    #
    # Embedded in the wasm rather than preloaded as a .data file. A preloaded file's URL is
    # fixed by the packager at the top of the module factory, before any pre-js runs, and in
    # a worker it resolves against the worker script's URL rather than this module's -- so a
    # host whose worker lives in another directory got a 404 and a promise that never
    # settled. Embedding costs ~66 MB of data segments (YAML compresses well on the wire)
    # and leaves the host two files that resolve themselves.
    "SHELL:--embed-file ${SOH_EXTRACT_RECIPE_DIR}@/work/assets"
    -sFORCE_FILESYSTEM=1
    # An ES module exporting a factory, usable from a module worker, a page, or node.
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createSohExtractor
    -sENVIRONMENT=web,worker,node
    "SHELL:-sEXPORTED_FUNCTIONS=_Extract_RomToO2r,_Extract_ResultJson,_malloc,_free"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString"
    # Desktop peaks at ~980 MB RSS for a 32 MB ROM (wasm-rom-extract.md); start small and
    # grow, but allow for the 64 MB debug ROMs.
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sMAXIMUM_MEMORY=4294967296
    -sSTACK_SIZE=4194304
    -sEXIT_RUNTIME=0
    --no-entry
    # Readable names in a browser stack trace; a name section, nothing at runtime.
    --profiling-funcs
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/pre.js"
    "SHELL:--post-js ${CMAKE_CURRENT_SOURCE_DIR}/api.js"
)

# The link line carries the embedded recipe and the JS files as options, which CMake does
# not track as dependencies; name them all so a change to any relinks soh-extract.wasm / .js.
file(GLOB_RECURSE SOH_EXTRACT_RECIPE CONFIGURE_DEPENDS ${SOH_EXTRACT_RECIPE_DIR}/*)
set_property(TARGET ${SOH_EXTRACT_NAME} APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pre.js
    ${CMAKE_CURRENT_SOURCE_DIR}/api.js
    ${SOH_EXTRACT_RECIPE}
)
