cmake_minimum_required(VERSION 3.17)
project(nfs_bench C)

set(CMAKE_C_STANDARD 11)

find_path(LIBNFS_INCLUDE_DIRS
  libnfs.h
  PATHS /usr/include/nfsc
  NO_DEFAULT_PATH)

find_library(LIBNFS_LIBRARIES
  NAME nfs
  PATHS /usr/lib64
  NO_DEFAULT_PATH)


include_directories(${LIBNFS_INCLUDE_DIRS})

add_executable(nfs_bench main.c)
target_link_libraries(nfs_bench ${LIBNFS_LIBRARIES})