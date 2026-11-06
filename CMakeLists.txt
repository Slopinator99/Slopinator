cmake_minimum_required(VERSION 3.21)
project(psxrdc2 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(psxrdc2 WIN32
    src/main.cpp
    src/Core.cpp
    src/Core.h
    src/Runner.cpp
    src/Runner.h
    src/Cli.cpp
    src/Library.cpp
    src/Library.h
    src/Chd.cpp
    src/Chd.h
    src/LibraryTab.cpp
    src/LibraryTab.h
    src/Cli.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/Setup.cpp
    src/Submission.cpp
    src/Setup.h
    src/Submission.h
    res/app.rc
)
target_link_libraries(psxrdc2 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
if(MINGW)
    target_link_options(psxrdc2 PRIVATE -static-libgcc -static-libstdc++)
    get_target_property(QT_CORE_TYPE Qt6::Core TYPE)
    message(STATUS "Qt6::Core type: ${QT_CORE_TYPE}")
    if(QT_CORE_TYPE STREQUAL "STATIC_LIBRARY")
        # Static Qt: also take every library it needs from its .a so the result is one exe.
        target_link_options(psxrdc2 PRIVATE -static)
    endif()
endif()
