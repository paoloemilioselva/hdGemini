include(FindPackageHandleStandardArgs)

set(TBB_SEARCH_DIR ${TBB_ROOT_DIR} $ENV{TBB_INSTALL_DIR} $ENV{TBBROOT})

find_path(TBB_INCLUDE_DIRS tbb/tbb.h
    HINTS ${TBB_SEARCH_DIR}
    PATH_SUFFIXES include)

if(TBB_INCLUDE_DIRS)
    if(EXISTS "${TBB_INCLUDE_DIRS}/tbb/tbb_stddef.h")
        file(READ "${TBB_INCLUDE_DIRS}/tbb/tbb_stddef.h" _tbb_version_file)
    elseif(EXISTS "${TBB_INCLUDE_DIRS}/oneapi/tbb/version.h")
        file(READ "${TBB_INCLUDE_DIRS}/oneapi/tbb/version.h" _tbb_version_file)
    endif()
    
    if(_tbb_version_file)
        string(REGEX REPLACE ".*#define TBB_VERSION_MAJOR ([0-9]+).*" "\\1"
            TBB_VERSION_MAJOR "${_tbb_version_file}")
        string(REGEX REPLACE ".*#define TBB_VERSION_MINOR ([0-9]+).*" "\\1"
            TBB_VERSION_MINOR "${_tbb_version_file}")
        set(TBB_VERSION "${TBB_VERSION_MAJOR}.${TBB_VERSION_MINOR}")
    endif()
endif()

find_library(TBB_LIBRARY_RELEASE tbb
    HINTS ${TBB_SEARCH_DIR}
    PATH_SUFFIXES lib lib64)

find_library(TBB_LIBRARY_DEBUG tbb_debug
    HINTS ${TBB_SEARCH_DIR}
    PATH_SUFFIXES lib lib64)

if(TBB_LIBRARY_RELEASE OR TBB_LIBRARY_DEBUG)
    set(TBB_FOUND TRUE)
endif()

find_package_handle_standard_args(TBB
    REQUIRED_VARS TBB_INCLUDE_DIRS TBB_LIBRARY_RELEASE
    VERSION_VAR TBB_VERSION)

if(TBB_FOUND)
    if(NOT TARGET TBB::tbb)
        add_library(TBB::tbb UNKNOWN IMPORTED)
        set_target_properties(TBB::tbb PROPERTIES
              INTERFACE_INCLUDE_DIRECTORIES  ${TBB_INCLUDE_DIRS})
        
        if(TBB_LIBRARY_RELEASE AND TBB_LIBRARY_DEBUG)
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_IMPLIB_RELEASE "${TBB_LIBRARY_RELEASE}"
                IMPORTED_LOCATION_RELEASE "${TBB_LIBRARY_RELEASE}"
                IMPORTED_IMPLIB_DEBUG   "${TBB_LIBRARY_DEBUG}"
                IMPORTED_LOCATION_DEBUG   "${TBB_LIBRARY_DEBUG}"
                IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
        elseif(TBB_LIBRARY_RELEASE)
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_IMPLIB "${TBB_LIBRARY_RELEASE}"
                IMPORTED_LOCATION "${TBB_LIBRARY_RELEASE}")
        else()
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_IMPLIB "${TBB_LIBRARY_DEBUG}"
                IMPORTED_LOCATION "${TBB_LIBRARY_DEBUG}")
        endif()
    endif()
endif()
