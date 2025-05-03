cmake_minimum_required(VERSION 3.30.3)

set(PROJECT_NAME smearin)
project(${PROJECT_NAME})

include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(SOURCE_FILES
    PluginMain.cpp
    cylinder.cpp
    motionLinesNode.cpp
    loadCacheCmd.cpp
    smear.cpp
    smearControlNode.cpp
    smearDeformerNode.cpp    
    smearNode.cpp
)

message(STATUS "SOURCE_FILES = ${SOURCE_FILES}")
list(REMOVE_DUPLICATES SOURCE_FILES)
message(STATUS "Final deduplicated sources: ${SOURCE_FILES}")
set(LIBRARIES
    OpenMaya 
    Foundation
    OpenMayaAnim
)

build_plugin()