set(TBB_ROOT_DIR ${USD_ROOT} )
if(NOT BOOST_FROM_USD)
    set( BOOST_FROM_USD 1_78)
endif()
set(BOOST_ROOT "${USD_ROOT}/include/boost-${BOOST_FROM_USD}" )
MESSAGE(STATUS "USD_VERSION = " ${USD_VERSION} )
MESSAGE(STATUS "USD_ROOT = " ${USD_ROOT} )
MESSAGE(STATUS "TBB_ROOT_DIR = " ${TBB_ROOT_DIR} )
MESSAGE(STATUS "BOOST_ROOT = " ${BOOST_ROOT} )
    
set(Python_ADDITIONAL_VERSIONS ${PYTHON_FOR_USD})
find_package(Python EXACT ${PYTHON_FOR_USD} COMPONENTS Interpreter Development)
include_directories( ${Python_INCLUDE_DIRS} )

get_filename_component(USD_INCLUDE_DIR ${USD_ROOT}/include ABSOLUTE)
get_filename_component(USD_LIBRARY_DIR ${USD_ROOT}/lib ABSOLUTE)

set(USD_LIBRARY_MONOLITHIC FALSE)
set(PXR_LIB_PREFIX "")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(USD
    REQUIRED_VARS
        USD_INCLUDE_DIR
        USD_LIBRARY_DIR
    VERSION_VAR
        USD_VERSION
)

find_package(Boost REQUIRED)
include_directories( ${BOOST_ROOT} )

set (_usd_libs
    usd_usdImagingGL
    usd_usdImaging
    usd_usdHydra
    usd_hdx
    usd_hdsi
    usd_hio
    usd_hdSt
    usd_hd
    usd_python
    usd_boost
    usd_glf
    usd_garch
    usd_pxOsd
    usd_usdRi
    usd_usdUI
    usd_usdShade
    usd_usdSkel
    usd_usdGeom
    usd_usd
    usd_usdUtils
    usd_pcp
    usd_sdf
    usd_plug
    usd_js
    usd_ar
    usd_work
    usd_tf
    usd_kind
    usd_arch
    usd_vt
    usd_gf
    usd_hf
    usd_cameraUtil
    usd_trace
)

include_directories(
    ${USD_INCLUDE_DIR}
)

set(USD_LIBS 
    ${Python_LIBRARIES}
    ${_usd_libs}
)
