cmake_minimum_required(VERSION 4.1)
project(relacy_lock_free_mpmc_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
find_package(relacy CONFIG REQUIRED)

add_executable(relacy_lock_free_mpmc_queue src/main.cpp src/relacy_lock_free_mpmc_queue.hpp)
target_link_libraries(relacy_lock_free_mpmc_queue PRIVATE relacy::relacy)