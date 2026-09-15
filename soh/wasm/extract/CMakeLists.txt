# SOH [WASM] ROM -> o2r converter as its own Emscripten module.
#
# The desktop game extracts in-process: Extract.cpp validates the ROM, then calls
# zapd_report() with the OTR exporter set, which writes oot.o2r. In a browser that job
# belongs in a worker, separate from the game, so this target builds ZAPD + OTRExporter +
# the pure ROM checks into `soh-extract.{js,wasm,data}` with one exported function.
# Background, measurements and the host contract: wasm-rom-extract.md, soh/wasm/HOST-API.md.
#
# ZAPD's and OTRExporter's own CMakeLists are not used here. ZAPD's adds -pthread on every
# non-Darwin target, which under Emscripten would switch the module to SharedArrayBuffer
# mode, and requires a libpng package that a cross build has no way to find. Their source
# lists are short and explicit, so they are listed here instead; the exporter's objects go
# straight into the executable, which keeps its static registration without --whole-archive.

set(SOH_EXTRACT_NAME soh-extract)
set(ZAPD_DIR ${CMAKE_SOURCE_DIR}/ZAPDTR/ZAPD)
set(ZAPD_LIB_DIR ${CMAKE_SOURCE_DIR}/ZAPDTR/lib)
set(OTREXPORTER_DIR ${CMAKE_SOURCE_DIR}/OTRExporter/OTRExporter)
set(SOH_EXTRACTOR_DIR ${CMAKE_SOURCE_DIR}/soh/soh/Extractor)

file(GLOB_RECURSE ZAPD_SOURCES CONFIGURE_DEPENDS ${ZAPD_DIR}/*.cpp ${ZAPD_DIR}/*.c)
# The standalone main, the console pathconf shim, ZOverlay (not in ZAPD's own build), and
# the crash handler, which needs execinfo.h for a backtrace that Emscripten cannot take;
# RomExtractor.cpp supplies its one entry point as a no-op.
list(FILTER ZAPD_SOURCES EXCLUDE REGEX "ExecutableMain\\.cpp$|pathconf\\.c$|/Overlays/|CrashHandler\\.cpp$")

set(LIBGFXD_SOURCES
    ${ZAPD_LIB_DIR}/libgfxd/gfxd.c
    ${ZAPD_LIB_DIR}/libgfxd/uc.c
    ${ZAPD_LIB_DIR}/libgfxd/uc_f3dex2.c
)

file(GLOB OTREXPORTER_SOURCES CONFIGURE_DEPENDS ${OTREXPORTER_DIR}/*.cpp)

set(SOH_EXTRACT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/RomExtractor.cpp
    ${SOH_EXTRACTOR_DIR}/RomInfo.cpp
    ${SOH_EXTRACTOR_DIR}/FastCrc32C.c
)

add_executable(${SOH_EXTRACT_NAME}
    ${SOH_EXTRACT_SOURCES}
    ${ZAPD_SOURCES}
    ${LIBGFXD_SOURCES}
    ${OTREXPORTER_SOURCES}
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
    ${ZAPD_DIR}
    ${ZAPD_LIB_DIR}/libgfxd
    ${OTREXPORTER_DIR}
    ${CMAKE_SOURCE_DIR}/soh
    ${CMAKE_SOURCE_DIR}/libultraship/include
    ${CMAKE_SOURCE_DIR}/libultraship/src
    ${CMAKE_SOURCE_DIR}/libultraship/src/resource
)

target_compile_definitions(${SOH_EXTRACT_NAME} PRIVATE
    GAME_OOT
    _CONSOLE
    _CRT_SECURE_NO_WARNINGS
    # Written into the archive as `portVersion`; the game refuses an archive from another
    # port version, so this must be the version of the soh.wasm it feeds.
    SOH_EXTRACT_PORT_VERSION="${CMAKE_PROJECT_VERSION}"
)

# ZAPD's own warning set: it is upstream code we do not fix here.
target_compile_options(${SOH_EXTRACT_NAME} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-missing-field-initializers
    -Wno-parentheses
    -Wno-narrowing
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    # ZAPD's ImageBackend links libpng (its PNG modes; never run here, always linked).
    # -fwasm-exceptions implies WASM_LEGACY_EXCEPTIONS as a Python bool, and the libpng
    # port script prints it into its own build flags as `True`, which emcc rejects. Naming
    # the setting explicitly, as an integer, is enough for the port to build.
    --use-port=libpng
    -sWASM_LEGACY_EXCEPTIONS=1
)

# libultraship supplies CRC64 (StrHash64) and, transitively, libzip, StormLib, spdlog,
# tinyxml2 and nlohmann_json. A static archive link pulls in only what is referenced, so
# none of the window, GL or audio code comes along.
target_link_libraries(${SOH_EXTRACT_NAME} PRIVATE libultraship)

set(SOH_EXTRACT_ASSETS ${CMAKE_SOURCE_DIR}/soh/assets)

target_link_options(${SOH_EXTRACT_NAME} PRIVATE
    --use-port=libpng
    -sWASM_LEGACY_EXCEPTIONS=1
    # The extraction recipe, at the paths ZAPD's version configs name relative to the
    # working directory: assets/xml/<version>/, assets/Config_<version>.xml, assets/symbols,
    # assets/filelists, assets/TexturePool.xml. All 14 versions ship, so the module can
    # detect the version itself. (Quoted as SHELL: so CMake keeps flag and path together.)
    #
    # Embedded in the wasm rather than preloaded as a .data file. A preloaded file's URL is
    # fixed by the packager at the top of the module factory, before any pre-js runs, and in
    # a worker it resolves against the worker script's URL rather than this module's -- so a
    # host whose worker lives in another directory got a 404 and a promise that never
    # settled. Embedding costs ~35 MB of data segments (XML compresses well on the wire)
    # and leaves the host two files that resolve themselves.
    "SHELL:--embed-file ${SOH_EXTRACT_ASSETS}/xml@/work/assets/xml"
    "SHELL:--embed-file ${SOH_EXTRACT_ASSETS}/extractor@/work/assets"
    -sFORCE_FILESYSTEM=1
    # An ES module exporting a factory, usable from a module worker, a page, or node.
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createSohExtractor
    -sENVIRONMENT=web,worker,node
    "SHELL:-sEXPORTED_FUNCTIONS=_Extract_RomToO2r,_Extract_ResultJson,_malloc,_free"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString"
    # Desktop peaks at ~514 MB RSS for a 32 MB ROM (wasm-rom-extract.md); start small
    # and grow, but allow for the 64 MB debug ROMs.
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sMAXIMUM_MEMORY=2147483648
    -sSTACK_SIZE=4194304
    -sEXIT_RUNTIME=0
    --no-entry
    # Readable names in a browser stack trace; a name section, nothing at runtime.
    --profiling-funcs
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/pre.js"
    "SHELL:--post-js ${CMAKE_CURRENT_SOURCE_DIR}/api.js"
)

# The link line carries the preload paths and the JS files as options, which CMake does not
# track as dependencies; name them so a change rebuilds soh-extract.data / .js.
set_property(TARGET ${SOH_EXTRACT_NAME} APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pre.js
    ${CMAKE_CURRENT_SOURCE_DIR}/api.js
    ${SOH_EXTRACT_ASSETS}/extractor/TexturePool.xml
)
