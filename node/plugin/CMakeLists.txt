cmake_minimum_required(VERSION 3.20)

# The Endstone side of the ABI firewall. Built with Endstone's toolchain (libc++ on Linux, MSVC ABI
# on Windows) and linked only against the header-only Endstone API. It must not link libnode.
endstone_add_plugin(nodejs plugin.cpp js_plugin_loader.cpp api_bridge.cpp)

target_include_directories(nodejs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if (NOT WIN32)
    target_link_libraries(nodejs PRIVATE ${CMAKE_DL_LIBS})
endif ()
