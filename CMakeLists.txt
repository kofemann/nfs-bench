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

find_package(MPI)
if (MPI_FOUND)
  add_definitions("-DHAVE_MPI")
  message (STATUS "MPI headers at ${MPI_C_INCLUDE_DIRS}")  
  message (STATUS "MPI libs ${MPI_C_LIBRARIES}")  
else()
  message (STATUS "MPI is not defined. Building simple version")  
endif()


include_directories(${LIBNFS_INCLUDE_DIRS} ${MPI_C_INCLUDE_DIRS})

add_executable(nfs_bench main.c)
target_link_libraries(nfs_bench ${LIBNFS_LIBRARIES} ${MPI_C_LIBRARIES})
