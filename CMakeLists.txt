cmake_minimum_required(VERSION 3.28)
project(DiskAnalyzer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if (WIN32 AND NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "A:/applications/Qt/6.8.1/mingw_64")
endif ()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

file(GLOB_RECURSE DISK_ANALYZER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
)
file(GLOB_RECURSE DISK_ANALYZER_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

qt_add_executable(DiskAnalyzer WIN32
        ${DISK_ANALYZER_SOURCES}
        ${DISK_ANALYZER_HEADERS}
        qt.conf
)

target_include_directories(DiskAnalyzer PRIVATE include)
target_link_libraries(DiskAnalyzer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)

if (MSVC)
    target_compile_options(DiskAnalyzer PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(DiskAnalyzer PRIVATE -Wall -Wextra -Wpedantic)
endif ()

if (WIN32)
    add_custom_command(TARGET DiskAnalyzer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/qt.conf"
            "$<TARGET_FILE_DIR:DiskAnalyzer>/qt.conf"
            COMMENT "Copying Qt plugin configuration"
    )
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${CMAKE_PREFIX_PATH}/bin")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DiskAnalyzer POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:DiskAnalyzer>"
                COMMENT "Deploying Qt runtime"
        )
    endif ()
endif ()

include(CTest)
if (BUILD_TESTING)
    qt_add_executable(scanner_tests
            tests/scanner_test.cpp
            source/services/directory_scanner.cpp
    )
    target_include_directories(scanner_tests PRIVATE include)
    target_link_libraries(scanner_tests PRIVATE Qt6::Core)
    if (MSVC)
        target_compile_options(scanner_tests PRIVATE /W4 /permissive- /utf-8)
    else ()
        target_compile_options(scanner_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
    add_test(NAME scanner_unicode COMMAND scanner_tests)
endif ()
