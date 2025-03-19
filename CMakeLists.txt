cmake_minimum_required(VERSION 3.30.3)

set(PROJECT_NAME smearin)
project(${PROJECT_NAME})

include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(SOURCE_FILES
    PluginMain.cpp
    smear.cpp
    smearNode.cpp
)

set(LIBRARIES
    OpenMaya 
    Foundation
    OpenMayaAnim
)

build_plugin()