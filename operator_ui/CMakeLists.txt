cmake_minimum_required(VERSION 3.16)
project(OmegaBotOperator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(OmegaBotOperator
    main.cpp
    MainWindow.h
    MainWindow.cpp
    RobotConnection.h
    RobotConnection.cpp
    VideoConnection.h
    VideoConnection.cpp
    Telemetry.h
)

target_link_libraries(OmegaBotOperator PRIVATE Qt6::Widgets Qt6::Network)
