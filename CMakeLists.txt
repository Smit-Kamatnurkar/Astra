cmake_minimum_required(VERSION 3.16)
project(astra LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── LLVM ────────────────────────────────────────────────────────────────
set(LLVM_DIR "/opt/homebrew/opt/llvm/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# ── Sources ─────────────────────────────────────────────────────────────
set(ASTRA_SOURCES
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
    src/codegen.cpp
)

add_executable(astra ${ASTRA_SOURCES})
target_include_directories(astra PRIVATE src)

# ── Link LLVM ───────────────────────────────────────────────────────────
llvm_map_components_to_libnames(LLVM_LIBS
    core
    support
    irreader
    passes
    aarch64asmparser
    aarch64codegen
    aarch64desc
    aarch64info
    x86asmparser
    x86codegen
    x86desc
    x86info
)
target_link_libraries(astra PRIVATE ${LLVM_LIBS})
