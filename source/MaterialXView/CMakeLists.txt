if (NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/NanoGUI/ext/glfw/src")
    message(FATAL_ERROR "Building the MaterialX viewer requires the NanoGUI submodule "
        "to be present. Update your repository by calling the following:\n"
        "git submodule update --init --recursive")
endif()

file(GLOB materialx_source "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB materialx_headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_SHARED OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON OFF CACHE BOOL " " FORCE)
set(NANOGUI_INSTALL OFF CACHE BOOL " " FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/NanoGUI external/NanoGUI)
set_property(TARGET nanogui nanogui-obj glfw glfw_objects PROPERTY FOLDER "External")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/NanoGUI/include
    ${NANOGUI_EXTRA_INCS})

add_definitions(${NANOGUI_EXTRA_DEFS})

if(MSVC)
    add_compile_options(/D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS)
endif()

add_executable(MaterialXView ${materialx_source} ${materialx_headers})

target_link_libraries(
    MaterialXView
    MaterialXFormat
    MaterialXGenGlsl
    MaterialXRender
    nanogui
    ${NANOGUI_EXTRA_LIBS})

add_custom_command(TARGET MaterialXView POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/documents/Libraries ${CMAKE_CURRENT_BINARY_DIR}/documents/Libraries)
add_custom_command(TARGET MaterialXView POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/documents/TestSuite ${CMAKE_CURRENT_BINARY_DIR}/documents/TestSuite)

install(TARGETS MaterialXView
        DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/documents/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/documents")
