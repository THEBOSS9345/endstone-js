cmake_minimum_required(VERSION 3.20)

# The Endstone side of the ABI firewall. Built with Endstone's toolchain (libc++ on Linux, MSVC ABI
# on Windows) and linked only against the header-only Endstone API. It must not link libnode.
# One binding file per Endstone header folder, so where a member goes is decided by where upstream
# put it rather than by which accessor happens to carry it. See plugin/types/bind.h.
file(GLOB_RECURSE NODEJS_TYPE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/types/*.cpp)

endstone_add_plugin(nodejs plugin.cpp js_plugin_loader.cpp api_bridge.cpp ${NODEJS_TYPE_SOURCES})

target_include_directories(nodejs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR})
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
        --types ${CMAKE_CURRENT_SOURCE_DIR}/types
        COMMAND ${CMAKE_COMMAND} -E touch ${METHOD_CHECK_STAMP}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api_bridge.cpp
        ${NODEJS_TYPE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../host/bootstrap.js
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_methods.py
        COMMENT "Checking the method table against the bridge"
        VERBATIM)
add_custom_target(nodejs_method_check DEPENDS ${METHOD_CHECK_STAMP})
add_dependencies(nodejs nodejs_method_check)

# A member can be bound and work at runtime while being invisible to anyone writing a plugin, because
# the TypeScript definitions are what they read. That is how Block went without x, y and z. The
# definitions live in a sibling checkout, so this passes quietly when it is not there.
set(TYPE_CHECK_STAMP ${CMAKE_CURRENT_BINARY_DIR}/types_checked.stamp)
add_custom_command(
        OUTPUT ${TYPE_CHECK_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_types.py
        --types ${CMAKE_CURRENT_SOURCE_DIR}/types
        --definitions ${CMAKE_CURRENT_SOURCE_DIR}/../../../endstone-server-types/index.d.ts
        COMMAND ${CMAKE_COMMAND} -E touch ${TYPE_CHECK_STAMP}
        DEPENDS ${NODEJS_TYPE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_types.py
        COMMENT "Checking the TypeScript definitions against the bindings"
        VERBATIM)
add_custom_target(nodejs_type_check DEPENDS ${TYPE_CHECK_STAMP})
add_dependencies(nodejs nodejs_type_check)

# Events are where upstream keeps adding, and a release can introduce one without anything noticing.
# The bindings mirror upstream's folder layout, so the gap is a directory diff.
set(EVENT_CHECK_STAMP ${CMAKE_CURRENT_BINARY_DIR}/events_checked.stamp)
add_custom_command(
        OUTPUT ${EVENT_CHECK_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_events.py
        --bindings ${CMAKE_CURRENT_SOURCE_DIR}/types/events
        --headers ${CMAKE_CURRENT_SOURCE_DIR}/../../include/endstone/event
        COMMAND ${CMAKE_COMMAND} -E touch ${EVENT_CHECK_STAMP}
        DEPENDS ${NODEJS_TYPE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_events.py
        COMMENT "Checking every upstream event is bound"
        VERBATIM)
add_custom_target(nodejs_event_check DEPENDS ${EVENT_CHECK_STAMP})
add_dependencies(nodejs nodejs_event_check)
