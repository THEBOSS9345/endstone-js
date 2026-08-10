cmake_minimum_required(VERSION 3.20)
project(endstone_node LANGUAGES C CXX)

# Matches the root Endstone CMakeLists: the public API headers use std::format and std::string_view.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (WIN32 AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # CMake cannot scan for C++20 module imports with clang-cl; nothing here uses modules.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif ()

# Milestone 0: prove Node.js can be embedded and run inside the Endstone/BDS process.
#
# Two independent binaries on purpose, because on Linux they cannot share a C++ runtime:
#
#   endstone_nodejs       Endstone plugin  - libc++ (Linux) / MSVC ABI (Windows), links endstone::endstone
#   endstone_node_host    Node.js host     - libstdc++ (Linux) / MSVC ABI (Windows), links libnode
#
# They only ever speak the C ABI in include/endstone_node_abi.h. Because the two halves need
# different toolchains on Linux, configure this directory twice there (see README.md); on Windows a
# single configure builds both.

option(ENDSTONE_NODE_BUILD_HOST "Build the Node.js host (needs libnode)" ON)
option(ENDSTONE_NODE_BUILD_PLUGIN "Build the Endstone-side plugin (needs the Endstone API)" ON)

set(ENDSTONE_NODE_LIBNODE_ROOT "" CACHE PATH
        "Root of a libnode distribution containing include/node and the import/shared library")

if (ENDSTONE_NODE_BUILD_HOST)
    # Preferred: the Conan package from recipes/libnode. Fallback: a libnode distribution pointed at
    # by ENDSTONE_NODE_LIBNODE_ROOT, e.g. one produced by node/scripts/fetch_libnode.py.
    if (NOT ENDSTONE_NODE_LIBNODE_ROOT)
        find_package(libnode CONFIG QUIET)
    endif ()

    if (libnode_FOUND)
        message(STATUS "libnode: Conan package")
        set(LIBNODE_LINK libnode::libnode)
    else ()
        find_path(LIBNODE_INCLUDE_DIR
                NAMES node.h
                HINTS "${ENDSTONE_NODE_LIBNODE_ROOT}/include/node" "${ENDSTONE_NODE_LIBNODE_ROOT}/include"
                NO_DEFAULT_PATH)
        find_library(LIBNODE_LIBRARY
                NAMES libnode node
                HINTS "${ENDSTONE_NODE_LIBNODE_ROOT}/lib" "${ENDSTONE_NODE_LIBNODE_ROOT}/bin"
                "${ENDSTONE_NODE_LIBNODE_ROOT}"
                NO_DEFAULT_PATH)
        if (NOT LIBNODE_INCLUDE_DIR OR NOT LIBNODE_LIBRARY)
            message(FATAL_ERROR
                    "libnode not found. Either install the Conan package (recipes/libnode) or run\n"
                    "  python node/scripts/fetch_libnode.py\n"
                    "and pass -DENDSTONE_NODE_LIBNODE_ROOT=<dir>. See node/README.md.")
        endif ()
        set(LIBNODE_LINK "${LIBNODE_LIBRARY}")
        message(STATUS "libnode headers : ${LIBNODE_INCLUDE_DIR}")
        message(STATUS "libnode library : ${LIBNODE_LIBRARY}")

        # On Windows the import library is not loadable; the DLL has to be staged beside the binaries.
        if (WIN32)
            find_file(LIBNODE_RUNTIME
                    NAMES libnode.dll
                    HINTS "${ENDSTONE_NODE_LIBNODE_ROOT}/bin" "${ENDSTONE_NODE_LIBNODE_ROOT}"
                    NO_DEFAULT_PATH)
        endif ()
    endif ()

    add_subdirectory(host)
endif ()

if (ENDSTONE_NODE_BUILD_PLUGIN)
    # Brings in endstone::endstone and endstone_add_plugin() without touching the Endstone tree.
    if (NOT TARGET endstone::endstone)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../include" endstone_api)
    endif ()
    add_subdirectory(plugin)
endif ()
