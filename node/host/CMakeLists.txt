cmake_minimum_required(VERSION 3.20)

# The Node side of the ABI firewall. Built with the toolchain libnode itself was built with
# (libstdc++ on Linux, MSVC ABI on Windows) - NOT Endstone's libc++ toolchain. It links no Endstone
# target and includes no Endstone header.
add_library(endstone_node_host SHARED host.cpp)
add_library(endstone::node_host ALIAS endstone_node_host)

target_include_directories(endstone_node_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if (LIBNODE_INCLUDE_DIR)
    target_include_directories(endstone_node_host PRIVATE ${LIBNODE_INCLUDE_DIR})
endif ()
# Either the Conan target libnode::libnode or a raw library path; both carry what we need.
target_link_libraries(endstone_node_host PRIVATE ${LIBNODE_LINK})
target_compile_features(endstone_node_host PRIVATE cxx_std_20)
target_compile_definitions(endstone_node_host PRIVATE ESN_HOST_BUILD)

if (WIN32)
    # node.h resolves NODE_EXTERN to dllimport only for consumers that declare themselves an
    # extension; without it every symbol would be dllexport and fail to link.
    target_compile_definitions(endstone_node_host PRIVATE
            BUILDING_NODE_EXTENSION
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00)
    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # v8config.h gates on __cplusplus, which the MSVC frontend misreports without this.
        target_compile_options(endstone_node_host PRIVATE /Zc:__cplusplus)
    endif ()
    set_target_properties(endstone_node_host PROPERTIES OUTPUT_NAME "endstone_node_host")
else ()
    # libnode's SONAME is versioned (libnode.so.<NODE_MODULE_VERSION>), so the linker has to be able
    # to resolve that name transitively when linking anything against the host. Only needed for the
    # raw-path fallback; the Conan package already exposes its own library directory.
    if (LIBNODE_LIBRARY)
        get_filename_component(LIBNODE_DIR "${LIBNODE_LIBRARY}" DIRECTORY)
    endif ()
    set_target_properties(endstone_node_host PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            # libnode is deployed next to the host; the Endstone plugin never needs it on its path.
            BUILD_RPATH "$ORIGIN;${LIBNODE_DIR}"
            INSTALL_RPATH "$ORIGIN")
    target_link_options(endstone_node_host PRIVATE "-Wl,-rpath-link,${LIBNODE_DIR}")
endif ()

# A C-only consumer. Proves the boundary needs no C++ runtime agreement at all.
add_executable(endstone_node_host_smoke standalone_main.c)
target_link_libraries(endstone_node_host_smoke PRIVATE endstone_node_host)
target_compile_features(endstone_node_host_smoke PRIVATE c_std_11)
if (NOT WIN32)
    set_target_properties(endstone_node_host_smoke PROPERTIES BUILD_RPATH "$ORIGIN;${LIBNODE_DIR}")
    target_link_options(endstone_node_host_smoke PRIVATE "-Wl,-rpath-link,${LIBNODE_DIR}")
endif ()

if (LIBNODE_RUNTIME)
    add_custom_command(TARGET endstone_node_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBNODE_RUNTIME}" "$<TARGET_FILE_DIR:endstone_node_host>"
            COMMENT "Staging libnode next to the host")
endif ()
