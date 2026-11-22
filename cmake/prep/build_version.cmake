# Set build variables if env variables are defined
# These are used in configured files such as manifests for different packages
if(DEFINED ENV{BRANCH})
    set(GITHUB_BRANCH $ENV{BRANCH})
endif()
set(BUILD_VERSION "" CACHE STRING "Version to report for this build, for example 0.4.6+local")
if(NOT "$ENV{BUILD_VERSION}" STREQUAL "")
    set(BUILD_VERSION "$ENV{BUILD_VERSION}")
endif()
if(DEFINED ENV{CLONE_URL})
    set(GITHUB_CLONE_URL $ENV{CLONE_URL})
endif()
if(DEFINED ENV{COMMIT})
    set(GITHUB_COMMIT $ENV{COMMIT})
endif()
if(DEFINED ENV{TAG})
    set(GITHUB_TAG $ENV{TAG})
endif()

if(NOT "${BUILD_VERSION}" STREQUAL "")
    string(REGEX REPLACE "^v" "" PROJECT_VERSION "${BUILD_VERSION}")
    set(CMAKE_PROJECT_VERSION "${PROJECT_VERSION}")
    if(NOT GITHUB_COMMIT AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE GITHUB_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()
else()
    # Generate Sunshine Version based of the git tag
    # https://github.com/nocnokneo/cmake-git-versioning-example/blob/master/LICENSE
    find_package(Git)
    if(GIT_EXECUTABLE)
        MESSAGE("${CMAKE_SOURCE_DIR}")
        get_filename_component(SRC_DIR "${CMAKE_SOURCE_DIR}" DIRECTORY)
        #Get current Branch
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                OUTPUT_VARIABLE GIT_DESCRIBE_BRANCH
                RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Gather current commit
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
                RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Check if Dirty
        execute_process(
                COMMAND ${GIT_EXECUTABLE} diff -b --quiet --exit-code
                RESULT_VARIABLE GIT_IS_DIRTY
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT GIT_DESCRIBE_ERROR_CODE)
            MESSAGE("Sunshine Branch: ${GIT_DESCRIBE_BRANCH}")
            if(NOT GIT_DESCRIBE_BRANCH STREQUAL "master")
                set(PROJECT_VERSION ${PROJECT_VERSION}.${GIT_DESCRIBE_VERSION})
                MESSAGE("Sunshine Version: ${GIT_DESCRIBE_VERSION}")
            endif()
            if(GIT_IS_DIRTY)
                set(PROJECT_VERSION ${PROJECT_VERSION}.dirty)
                MESSAGE("Git tree is dirty!")
            endif()
        else()
            MESSAGE(ERROR ": Got git error while fetching tags: ${GIT_DESCRIBE_ERROR_CODE}")
        endif()
    else()
        MESSAGE(WARNING ": Git not found, cannot find git version")
    endif()
endif()

# set date variables
set(PROJECT_YEAR "1990")
set(PROJECT_MONTH "01")
set(PROJECT_DAY "01")

# Extract year, month, and day (do this AFTER version parsing)
# Note: Cmake doesn't support "{}" regex syntax
if(PROJECT_VERSION MATCHES "^([0-9][0-9][0-9][0-9])\\.([0-9][0-9][0-9][0-9]?)\\.([0-9]+)$")
    message("Extracting year and month/day from PROJECT_VERSION: ${PROJECT_VERSION}")
    # First capture group is the year
    set(PROJECT_YEAR "${CMAKE_MATCH_1}")

    # Second capture group contains month and day
    set(MONTH_DAY "${CMAKE_MATCH_2}")

    # Extract month (first 1-2 digits) and day (last 2 digits)
    string(LENGTH "${MONTH_DAY}" MONTH_DAY_LENGTH)
    if(MONTH_DAY_LENGTH EQUAL 3)
        # Format: MDD (e.g., 703 = month 7, day 03)
        string(SUBSTRING "${MONTH_DAY}" 0 1 PROJECT_MONTH)
        string(SUBSTRING "${MONTH_DAY}" 1 2 PROJECT_DAY)
    elseif(MONTH_DAY_LENGTH EQUAL 4)
        # Format: MMDD (e.g., 1203 = month 12, day 03)
        string(SUBSTRING "${MONTH_DAY}" 0 2 PROJECT_MONTH)
        string(SUBSTRING "${MONTH_DAY}" 2 2 PROJECT_DAY)
    endif()

    # Ensure month is two digits
    if(PROJECT_MONTH LESS 10 AND NOT PROJECT_MONTH MATCHES "^0")
        set(PROJECT_MONTH "0${PROJECT_MONTH}")
    endif()
    # Ensure day is two digits
    if(PROJECT_DAY LESS 10 AND NOT PROJECT_DAY MATCHES "^0")
        set(PROJECT_DAY "0${PROJECT_DAY}")
    endif()
endif()

# Parse PROJECT_VERSION to extract major, minor, and patch components
if(PROJECT_VERSION MATCHES "([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(CMAKE_PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")

    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(CMAKE_PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")

    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(CMAKE_PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
endif()

message("PROJECT_NAME: ${PROJECT_NAME}")
message("PROJECT_VERSION: ${PROJECT_VERSION}")
message("PROJECT_VERSION_MAJOR: ${PROJECT_VERSION_MAJOR}")
message("PROJECT_VERSION_MINOR: ${PROJECT_VERSION_MINOR}")
message("PROJECT_VERSION_PATCH: ${PROJECT_VERSION_PATCH}")
message("CMAKE_PROJECT_VERSION: ${CMAKE_PROJECT_VERSION}")
message("CMAKE_PROJECT_VERSION_MAJOR: ${CMAKE_PROJECT_VERSION_MAJOR}")
message("CMAKE_PROJECT_VERSION_MINOR: ${CMAKE_PROJECT_VERSION_MINOR}")
message("CMAKE_PROJECT_VERSION_PATCH: ${CMAKE_PROJECT_VERSION_PATCH}")
message("PROJECT_YEAR: ${PROJECT_YEAR}")
message("PROJECT_MONTH: ${PROJECT_MONTH}")
message("PROJECT_DAY: ${PROJECT_DAY}")

list(APPEND SUNSHINE_DEFINITIONS PROJECT_NAME="${PROJECT_NAME}")
list(APPEND SUNSHINE_DEFINITIONS PROJECT_VERSION="${PROJECT_VERSION}")
list(APPEND SUNSHINE_DEFINITIONS PROJECT_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}")
list(APPEND SUNSHINE_DEFINITIONS PROJECT_VERSION_MINOR="${PROJECT_VERSION_MINOR}")
list(APPEND SUNSHINE_DEFINITIONS PROJECT_VERSION_PATCH="${PROJECT_VERSION_PATCH}")
list(APPEND SUNSHINE_DEFINITIONS PROJECT_VERSION_COMMIT="${GITHUB_COMMIT}")
