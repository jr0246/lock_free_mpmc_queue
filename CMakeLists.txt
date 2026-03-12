cmake_minimum_required(VERSION 4.1)
project(lock_free_mpmc_queue)

set(CMAKE_CXX_STANDARD 23)
set(BOOST_ROOT /usr/local/boost_1_90_0)
find_package(Boost REQUIRED)

add_executable(lock_free_mpmc_queue src/main.cpp
        src/lock_free_mpmc_queue.hpp
)

add_executable(q_bandwidth
        src/q_bandwidth.cpp
        src/lock_free_mpmc_queue.hpp
        src/mpmc_queue_timing.h
)
target_link_libraries(q_bandwidth LINK_PRIVATE pthread)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
        lock_free_mpmc_queue_test
        test/lock_free_mpmc_queue_test.cpp
)
target_link_libraries(
        lock_free_mpmc_queue_test
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(lock_free_mpmc_queue_test)