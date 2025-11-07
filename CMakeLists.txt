cmake_minimum_required(VERSION 3.21)

project(cclone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CCLONE_ENABLE_TESTS "Enable building unit tests" ON)

find_package(Git QUIET)

set(CCLONE_GIT_BRANCH "unknown")
set(CCLONE_GIT_COMMIT "unknown")
set(CCLONE_GIT_COMMIT_SHORT "unknown")
set(CCLONE_GIT_TAG "unknown")
set(CCLONE_GIT_DIRTY "false")

if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CCLONE_GIT_BRANCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CCLONE_GIT_COMMIT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CCLONE_GIT_COMMIT_SHORT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --always
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CCLONE_GIT_TAG
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} status --porcelain
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CCLONE_GIT_STATUS
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	if(CCLONE_GIT_STATUS)
		set(CCLONE_GIT_DIRTY "true")
	endif()

	foreach(var IN ITEMS CCLONE_GIT_BRANCH CCLONE_GIT_COMMIT CCLONE_GIT_COMMIT_SHORT CCLONE_GIT_TAG)
		if(NOT ${var})
			set(${var} "unknown")
		endif()
	endforeach()
endif()

string(TIMESTAMP CCLONE_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

configure_file(
	${CMAKE_SOURCE_DIR}/cmake/git_info.hpp.in
	${CMAKE_BINARY_DIR}/generated/git_info.hpp
	@ONLY
)

# Dependency discovery is handled by vcpkg or Conan manifests.
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

if(NOT TARGET spdlog::spdlog AND TARGET spdlog::spdlog_header_only)
	add_library(spdlog::spdlog ALIAS spdlog::spdlog_header_only)
endif()

add_library(cclone INTERFACE)
target_include_directories(cclone
	INTERFACE
		src
		$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)
target_link_libraries(cclone
	INTERFACE
		fmt::fmt
		spdlog::spdlog
		CLI11::CLI11
)

if(CCLONE_ENABLE_TESTS)
	enable_testing()

	find_package(GTest CONFIG QUIET)
	if(NOT GTest_FOUND)
		find_package(gtest CONFIG QUIET)
	endif()

	if(NOT (TARGET GTest::gtest OR TARGET gtest::gtest OR TARGET gtest))
		include(FetchContent)
		message(STATUS "GoogleTest package not found via package managers. Fetching v1.14.0 from upstream...")
		set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
		FetchContent_Declare(
			googletest
			GIT_REPOSITORY https://github.com/google/googletest.git
			GIT_TAG v1.14.0
			GIT_SHALLOW TRUE
		)
		FetchContent_MakeAvailable(googletest)
	endif()

	if(TARGET GTest::gtest)
		set(CCLONE_GTEST_LIB GTest::gtest)
		set(CCLONE_GTEST_MAIN_LIB GTest::gtest_main)
	elseif(TARGET gtest::gtest)
		set(CCLONE_GTEST_LIB gtest::gtest)
		set(CCLONE_GTEST_MAIN_LIB gtest::gtest_main)
	elseif(TARGET gtest)
		set(CCLONE_GTEST_LIB gtest)
		set(CCLONE_GTEST_MAIN_LIB gtest_main)
	else()
		message(FATAL_ERROR "GoogleTest still unavailable after FetchContent.")
	endif()

	add_executable(cclone_tests
		tests/sample_test.cpp
	)

	target_link_libraries(cclone_tests
		PRIVATE
			cclone
			${CCLONE_GTEST_LIB}
			${CCLONE_GTEST_MAIN_LIB}
	)

	add_test(NAME cclone_tests COMMAND cclone_tests)
endif()
