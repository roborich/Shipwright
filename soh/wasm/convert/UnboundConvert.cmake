# SOH [Unbound] [WASM] soh-unbound-convert: oot.o2r -> oot-unbound.o2r as its own Emscripten module, so a host
# can have the Unbound base before the game runs (UnboundConvert.cpp; host contract: soh/wasm/HOST-API.md §7).
#
# Included from soh/CMakeLists.txt once the game target exists. The converter needs the game's resource factories
# and scene registry, which reach deep into the decomp, so rather than list a subset it links the game's own
# object files (nothing compiles twice) with UnboundConvert.cpp's exports as the entry points. main.c comes along
# for the globals it defines, but main() is never called (INVOKE_RUN=0). The linker keeps what the conversion
# reaches plus the objects' static initialisers; no window, audio or frame loop starts.

set(SOH_UNBOUND_CONVERT_NAME soh-unbound-convert)
set(SOH_UNBOUND_CONVERT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm/convert)

add_executable(${SOH_UNBOUND_CONVERT_NAME}
    ${SOH_UNBOUND_CONVERT_DIR}/UnboundConvert.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}>
)

set_target_properties(${SOH_UNBOUND_CONVERT_NAME} PROPERTIES
    CXX_STANDARD 20
    C_STANDARD 11
    OUTPUT_NAME ${SOH_UNBOUND_CONVERT_NAME}
    SUFFIX ".js"
    # Next to soh.js and soh-extract.js, so the wasm artifacts are one directory.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# The entry file includes game headers exactly as the game's sources do.
target_include_directories(${SOH_UNBOUND_CONVERT_NAME} PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
target_compile_definitions(${SOH_UNBOUND_CONVERT_NAME} PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>)
target_compile_options(${SOH_UNBOUND_CONVERT_NAME} PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_OPTIONS>)

target_link_libraries(${SOH_UNBOUND_CONVERT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")

target_link_options(${SOH_UNBOUND_CONVERT_NAME} PRIVATE
    -sFORCE_FILESYSTEM=1
    # An ES module exporting a factory, usable from a module worker, a page, or node -- as soh-extract.
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createSohUnboundConverter
    -sENVIRONMENT=web,worker,node
    "SHELL:-sEXPORTED_FUNCTIONS=_Unbound_ConvertArchive,_Unbound_ConvertResultJson,_malloc,_free"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString"
    # The source archive (~33 MB) and the base (~65 MB) live in MEMFS outside the heap; the heap holds the
    # loaded resources and the converter's documents.
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sMAXIMUM_MEMORY=2147483648
    -sSTACK_SIZE=4194304
    -sEXIT_RUNTIME=0
    -sINVOKE_RUN=0
    # The game's two guards against Binaryen building functions V8 cannot compile (see soh/CMakeLists.txt): the
    # cap on single-caller inlining, and exported functions, which are never inlined. Without the exports,
    # InitTrickNames (kept alive by a randomizer hook's initialiser) grew to 10663 locals; the artifacts test in
    # soh/wasm/tests checks both modules.
    "SHELL:-sBINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=1000"
    -Wl,-export-dynamic
    # Readable names in a browser stack trace; a name section, nothing at runtime.
    --profiling-funcs
    "SHELL:--post-js ${SOH_UNBOUND_CONVERT_DIR}/api.js"
)

set_property(TARGET ${SOH_UNBOUND_CONVERT_NAME} APPEND PROPERTY LINK_DEPENDS ${SOH_UNBOUND_CONVERT_DIR}/api.js)
