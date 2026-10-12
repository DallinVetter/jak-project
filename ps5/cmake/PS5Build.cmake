# Native PS5 build: the dependency closure of gk and nothing else.
#
# Included from the top-level CMakeLists.txt in place of the desktop dependency
# list. Produces static archives and the gk main object; see ps5/build_gk.sh.

if(NOT PS5_SDL3_ROOT OR NOT EXISTS "${PS5_SDL3_ROOT}/cmake/sdl/libSDL3.a")
    message(FATAL_ERROR "PS5_SDL3_ROOT must be ps5-opengl's SDL3 native build (build/sdl3-native)")
endif()

# Line tables are enough to turn a crash address into a source line.
string(REPLACE "-ggdb -g" "-g1" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# SDL: ps5-opengl's patched build of the same release OpenGOAL vendors. Sources
# name the vendored headers by path, so the patched ones are overlaid on it.
set(PS5_SDL_INCLUDE "${PS5_SDL3_ROOT}/SDL/include")
set(PS5_INCLUDE_OVERLAY "${CMAKE_BINARY_DIR}/ps5-include-overlay")
file(MAKE_DIRECTORY "${PS5_INCLUDE_OVERLAY}/third-party/SDL")
file(CREATE_LINK "${PS5_SDL_INCLUDE}" "${PS5_INCLUDE_OVERLAY}/third-party/SDL/include" SYMBOLIC)
add_library(SDL3::SDL3 STATIC IMPORTED GLOBAL)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${PS5_SDL3_ROOT}/cmake/sdl/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${PS5_SDL_INCLUDE}")

include_directories(BEFORE "${PS5_INCLUDE_OVERLAY}")
include_directories("${PROJECT_SOURCE_DIR}")
include_directories(SYSTEM "${PROJECT_SOURCE_DIR}/third-party/inja")
include_directories("${PROJECT_SOURCE_DIR}/third-party/fmt/include")
include_directories("${PROJECT_SOURCE_DIR}/third-party/glad/include")
include_directories("${PS5_SDL_INCLUDE}")

build_third_party_lib(fmt fmt)
build_third_party_lib(lzokay lzokay)
build_third_party_lib(zstd libzstd_static)
# zstd's tracing hooks are weak references, which the title converter rejects.
target_compile_definitions(libzstd_static PRIVATE ZSTD_TRACE=0)
build_third_party_lib(stb_image stb_image)
build_third_party_lib(libco libco)
build_third_party_lib(imgui imgui)
# ImGui's OpenGL backend bundles its own GL loader, which opens libGL with dlopen. GL is linked
# into the title, so make the backend use the entry points gk has already loaded through glad.
set_source_files_properties("${PROJECT_SOURCE_DIR}/third-party/imgui/imgui_impl_opengl3.cpp"
    TARGET_DIRECTORY imgui PROPERTIES
    COMPILE_DEFINITIONS IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    COMPILE_OPTIONS "-include;${PROJECT_SOURCE_DIR}/third-party/glad/include/glad/glad.h")

# Same options as the desktop build (Jak 2/3's built in editor).
add_definitions(-DHAVE_USLEEP=1)
add_definitions(-DSQLITE_THREADSAFE=1)
add_definitions(-DSQLITE_ENABLE_JSON1)
add_definitions(-DSQLITE_ENABLE_RTREE)
build_third_party_lib(sqlite3 sqlite3)

# Libraries whose headers common/ and game/ include but which the console build
# does not use. Their targets exist so those directories' CMake files are unchanged;
# the few entry points the runtime calls are provided by ps5/platform.
function(ps5_headers_only name)
    add_library(${name} INTERFACE)
    target_include_directories(${name} SYSTEM INTERFACE ${ARGN})
endfunction()
ps5_headers_only(replxx "${PROJECT_SOURCE_DIR}/third-party/replxx/include")
ps5_headers_only(tree-sitter "${PROJECT_SOURCE_DIR}/third-party/tree-sitter/tree-sitter/lib/include")
ps5_headers_only(libtinyfiledialogs "${PROJECT_SOURCE_DIR}/third-party/libtinyfiledialogs")
ps5_headers_only(tiny_gltf "${PROJECT_SOURCE_DIR}/third-party/tiny_gltf")
ps5_headers_only(discord-rpc "${PROJECT_SOURCE_DIR}/third-party/discord-rpc/include")
ps5_headers_only(libcurl "${PROJECT_SOURCE_DIR}/third-party/curl/include")
target_compile_definitions(libcurl INTERFACE CURL_STATICLIB)
ps5_headers_only(cubeb "${PROJECT_SOURCE_DIR}/third-party/cubeb/cubeb/include"
                       "${PROJECT_SOURCE_DIR}/third-party/cubeb/include")

add_subdirectory(common)
add_subdirectory(game)

file(GLOB PS5_PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/ps5/platform/*.cpp" "${PROJECT_SOURCE_DIR}/ps5/platform/*.c")
add_library(ps5_platform STATIC ${PS5_PLATFORM_SOURCES})
target_link_libraries(ps5_platform PRIVATE cubeb discord-rpc libcurl libtinyfiledialogs replxx fmt)

# gk's entry point as a bare object: CMake cannot link a PS5 executable.
add_library(gk_main OBJECT game/main.cpp)
target_link_libraries(gk_main PRIVATE runtime)

add_custom_target(ps5_gk DEPENDS gk_main runtime sound common ps5_platform
    fmt lzokay libzstd_static stb_image libco imgui sqlite3)
