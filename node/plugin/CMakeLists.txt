cmake_minimum_required(VERSION 3.20)

# The Endstone side of the ABI firewall. Built with Endstone's toolchain (libc++ on Linux, MSVC ABI
# on Windows) and linked only against the header-only Endstone API. It must not link libnode.
endstone_add_plugin(nodejs plugin.cpp js_plugin_loader.cpp api_bridge.cpp)

target_include_directories(nodejs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if (NOT WIN32)
    target_link_libraries(nodejs PRIVATE ${CMAKE_DL_LIBS})
endif ()

# The bridge decides which methods each type has; the runtime's METHODS_BY_TYPE decides which ones
# JavaScript will even try to call. A disagreement is invisible until someone hits it at runtime, so
# it fails the build instead.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(METHOD_CHECK_STAMP ${CMAKE_CURRENT_BINARY_DIR}/methods_checked.stamp)
add_custom_command(
        OUTPUT ${METHOD_CHECK_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_methods.py
        --bridge ${CMAKE_CURRENT_SOURCE_DIR}/api_bridge.cpp
        --runtime ${CMAKE_CURRENT_SOURCE_DIR}/../host/bootstrap.js
        COMMAND ${CMAKE_COMMAND} -E touch ${METHOD_CHECK_STAMP}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api_bridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../host/bootstrap.js
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_methods.py
        COMMENT "Checking the method table against the bridge"
        VERBATIM)
add_custom_target(nodejs_method_check DEPENDS ${METHOD_CHECK_STAMP})
add_dependencies(nodejs nodejs_method_check)
