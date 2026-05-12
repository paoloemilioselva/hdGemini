cmake_minimum_required(VERSION 3.23)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET "hdGemini")
add_definitions(-DNOMINMAX)

option(HDGEMINI_DEBUG_PRINTS "Enable verbose debug printing" OFF)
if(HDGEMINI_DEBUG_PRINTS)
    add_compile_definitions(HDGEMINI_DEBUG_PRINTS)
endif()

option(HDGEMINI_USE_SYCL "Enable SYCL backend for ray generation" OFF)

project( 
    ${TARGET}
    LANGUAGES CXX C
)

if (CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
    set(HDGEMINI_USE_SYCL ON CACHE BOOL "Enable SYCL backend for ray generation" FORCE)
    message(STATUS "Intel oneAPI compiler detected. SYCL backend ENABLED automatically.")
endif()

set( DELEGATE_NAME ${TARGET} )

# check target config
if(NOT TARGET_CONFIG)
    set( TARGET_CONFIG "usd-26.03")
endif()

string(REPLACE "-" ";" _t_config ${TARGET_CONFIG})
list(LENGTH _t_config _t_config_length)
if(_t_config_length LESS 2)
    MESSAGE(FATAL_ERROR "Wrong TARGET_CONFIG value " ${TARGET_CONFIG} )
endif()

list(GET _t_config 0 _target_name )

set( USD_LIBS "" )

if( _target_name MATCHES "houdini" )
    list(GET _t_config 1 HOUDINI_MAJ_MIN )
    list(GET _t_config 2 HOUDINI_PATCH )
    list(GET _t_config 3 HOUDINI_PYTHON_VERSION )
    set( HOUDINI_VERSION "Houdini ${HOUDINI_MAJ_MIN}.${HOUDINI_PATCH}" )
    set( HOUDINI_ROOT "C:\\Program Files\\Side Effects Software\\${HOUDINI_VERSION}" )

elseif( _target_name MATCHES "usd" )
    list(GET _t_config 1 USD_VERSION )
    set(USD_ROOT "C:\\dev\\usd-${USD_VERSION}" )

else()
    MESSAGE(FATAL_ERROR "Invalid target " ${_target_name})
endif()

include(cmake/FindUsd.cmake)
include(cmake/FindTBB.cmake)

# --- Open Image Denoise (OIDN) ---
find_package(OpenImageDenoise QUIET)
if(NOT OpenImageDenoise_FOUND)
    message(STATUS "OIDN not found locally. Downloading binary release for Windows...")
    include(FetchContent)
    FetchContent_Declare(
        oidn_bin
        URL https://github.com/OpenImageDenoise/oidn/releases/download/v2.2.2/oidn-2.2.2.x64.windows.zip
    )
    FetchContent_Populate(oidn_bin)
    set(OpenImageDenoise_ROOT ${oidn_bin_SOURCE_DIR} CACHE PATH "OIDN root directory")
    set(OpenImageDenoise_DIR ${OpenImageDenoise_ROOT}/lib/cmake/OpenImageDenoise)
    find_package(OpenImageDenoise REQUIRED)
else()
    # Try to derive ROOT from DIR if it was found via cached DIR
    if(NOT OpenImageDenoise_ROOT AND OpenImageDenoise_DIR)
        get_filename_component(OpenImageDenoise_ROOT "${OpenImageDenoise_DIR}/../../.." ABSOLUTE)
    endif()
endif()

if(OpenImageDenoise_FOUND)
    set(OIDN_LIBRARIES OpenImageDenoise)
    add_definitions(-DHDGEMINI_HAS_OIDN)
    message(STATUS "OIDN found. Denoising enabled. ROOT: ${OpenImageDenoise_ROOT}")
    
    # Copy OIDN DLLs to the build folder for runtime discovery
    file(GLOB OIDN_DLLS "${OpenImageDenoise_ROOT}/bin/*.dll")
    foreach(DLL ${OIDN_DLLS})
        file(COPY ${DLL} DESTINATION ${CMAKE_BINARY_DIR})
    endforeach()
endif()

add_library( ${DELEGATE_NAME} SHARED
    rendererPlugin.cpp
    rendererPlugin.h
    renderDelegate.cpp
    renderDelegate.h
    renderPass.cpp
    renderPass.h
    renderBuffer.cpp
    renderBuffer.h
    mesh.cpp
    mesh.h
    camera.cpp
    camera.h
    instancer.cpp
    instancer.h
    light.cpp
    light.h
    material.cpp
    material.h
    renderer.cpp
    renderer.h
    bvh.cpp
    bvh.h
    spectrum.cpp
    spectrum.h
    renderParam.h
    implicitSurfaceSceneIndexPlugin.cpp
    implicitSurfaceSceneIndexPlugin.h
)

target_compile_options( 
    ${DELEGATE_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4273 /Zi>
)

target_link_directories( 
    ${DELEGATE_NAME}
    PRIVATE 
    ${USD_LIBRARY_DIR}
)

target_link_libraries(
    ${DELEGATE_NAME}
    PUBLIC
    ${USD_LIBS}
    usd_sdr.lib
    TBB::tbb
    ${OIDN_LIBRARIES}
)

if(HDGEMINI_USE_SYCL)
    add_compile_definitions(HDGEMINI_HAS_SYCL)
    target_compile_options(${DELEGATE_NAME} PRIVATE -fsycl)
    target_link_options(${DELEGATE_NAME} PRIVATE -fsycl)
endif()

set(_installation_folder "")
if( _target_name MATCHES "houdini" )
    set(_installation_folder ${HOUDINI_ROOT_USER}/${DELEGATE_NAME})
    install(TARGETS ${DELEGATE_NAME} DESTINATION ${_installation_folder}/dso/usd_plugins)
    install(FILES ${CMAKE_SOURCE_DIR}/plugInfo.json DESTINATION ${_installation_folder}/dso/usd_plugins)

elseif( _target_name MATCHES "usd" )
    set(_installation_folder "C:/Users/paolo/Desktop/usd-26.03-extra")
    install(TARGETS ${DELEGATE_NAME} RUNTIME DESTINATION ${_installation_folder}/plugin/usd)
    install(FILES ${CMAKE_SOURCE_DIR}/usd-extra/plugInfo.json DESTINATION ${_installation_folder}/plugin/usd/${DELEGATE_NAME}/resources )
    
    # Install OIDN DLLs
    if(OpenImageDenoise_FOUND)
        file(GLOB OIDN_DLLS "${OpenImageDenoise_ROOT}/bin/*.dll")
        install(FILES ${OIDN_DLLS} DESTINATION ${_installation_folder}/lib)
    endif()
endif()
