find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBBARESIP QUIET libbaresip)

find_path(BARESIP_INCLUDE_DIR baresip.h
  HINTS ${PC_LIBBARESIP_INCLUDEDIR} ${PC_LIBBARESIP_INCLUDE_DIRS})

find_library(BARESIP_LIBRARY NAMES baresip libbaresip
  HINTS ${PC_LIBBARESIP_LIBDIR} ${PC_LIBBARESIP_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BARESIP DEFAULT_MSG
  BARESIP_LIBRARY BARESIP_INCLUDE_DIR)

mark_as_advanced(BARESIP_INCLUDE_DIR BARESIP_LIBRARY)

set(BARESIP_INCLUDE_DIRS ${BARESIP_INCLUDE_DIR})
set(BARESIP_LIBRARIES ${BARESIP_LIBRARY})
