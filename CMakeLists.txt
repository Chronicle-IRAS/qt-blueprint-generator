cmake_minimum_required(VERSION 3.21)

project(BlueprintEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_library(BlueprintEditorCore STATIC
    src/app/main_window.cpp
    src/app/main_window.h
)

target_include_directories(BlueprintEditorCore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(BlueprintEditorCore PUBLIC
    Qt6::Widgets
)

qt_add_executable(BlueprintEditor
    src/main.cpp
)

target_link_libraries(BlueprintEditor PRIVATE
    BlueprintEditorCore
)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
