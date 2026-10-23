cmake_minimum_required(VERSION 3.16)
project(asyncfuture VERSION 1.0.0 LANGUAGES CXX)

option(ENABLE_TESTING "Enable testing for the project" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_library(asyncfuture STATIC
    asyncfuture.h
    asyncfuture.cpp
)
set_target_properties(asyncfuture PROPERTIES AUTOMOC ON)
target_link_libraries(asyncfuture PUBLIC Qt6::Core)
target_include_directories(asyncfuture PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()

set_target_properties(asyncfuture PROPERTIES PUBLIC_HEADER "asyncfuture.h")

install(TARGETS asyncfuture
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include)

export(TARGETS asyncfuture NAMESPACE asyncfuture:: FILE asyncfutureConfig.cmake)
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE asyncfuture)
