# Intel (x86_64) build of ATK Player's vcpkg dependencies.
#
# The stock x64-osx triplet links statically and targets whatever macOS the
# build machine runs. ATK Player needs neither: FFmpeg is LGPL and must stay
# dynamically linked (as the Windows DLLs are), and the libraries must load on
# the same minimum macOS the application itself targets.
#
# Keep VCPKG_OSX_DEPLOYMENT_TARGET equal to CMAKE_OSX_DEPLOYMENT_TARGET in
# CMakeLists.txt.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
