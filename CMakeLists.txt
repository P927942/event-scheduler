cmake_minimum_required(VERSION 3.10)
project(EventCalendarScheduler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# ── Shared library (tree + manager logic) ────────────────────────────────────
add_library(scheduler_core STATIC
    IntervalTree.cpp
    CalendarManager.cpp
)
target_include_directories(scheduler_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── Server binary ─────────────────────────────────────────────────────────────
add_executable(calendar_engine main.cpp)
target_link_libraries(calendar_engine PRIVATE scheduler_core Threads::Threads)

# ── Unit-test binary ──────────────────────────────────────────────────────────
add_executable(run_tests tests.cpp)
target_link_libraries(run_tests PRIVATE scheduler_core Threads::Threads)

# Register with CTest so `cmake --build build --target test` or `ctest` works
enable_testing()
add_test(NAME UnitTests COMMAND run_tests)

message(STATUS "Build targets: calendar_engine, run_tests")
message(STATUS "Run tests with: cmake --build build --target test")
