CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(COCAINE-PLUGINS)

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

OPTION(CACHE "Build LRU Cache plugin" ON)
OPTION(CHRONO "Build Chrono Service plugin" ON)
OPTION(DOCKER "Build Docker Isolation plugin" ON)
OPTION(ELASTICSEARCH "Build ElasticSearch Service plugin" ON)
OPTION(IPVS "Build IPVS Gateway Service plugin" ON)
OPTION(MONGO "Build MongoDB Storage plugin" OFF)
OPTION(NODE "Build Node service plugin" ON)
OPTION(URLFETCH "Build URLFetch Service plugin" ON)
OPTION(GRAPHITE "Build Graphite Service plugin" ON)
OPTION(UNICORN "Build Unified configuration service plugin" ON)

# Include our CMake libraries.
INCLUDE(cmake/locate_library.cmake)

FIND_PACKAGE(Boost 1.46.0
    REQUIRED COMPONENTS
        system
        filesystem
        thread)

LOCATE_LIBRARY(LIBCOCAINE "cocaine/context.hpp" "cocaine-core")
LOCATE_LIBRARY(LIBMSGPACK "msgpack.hpp" "msgpack")

# System is required to get rid of errors in foreign libs
INCLUDE_DIRECTORIES(
    SYSTEM
    ${Boost_INCLUDE_DIRS}
    ${LIBCOCAINE_INCLUDE_DIRS}
    ${LIBMSGPACK_INCLUDE_DIRS})

# System is required to get rid of errors in foreign libs
INCLUDE_DIRECTORIES(BEFORE
    SYSTEM
    ${PROJECT_SOURCE_DIR}/foreign/asio/asio/include
    ${PROJECT_SOURCE_DIR}/foreign/blackhole/src
    ${PROJECT_SOURCE_DIR}/foreign/rapidjson/include)

LINK_DIRECTORIES(
    ${Boost_LIBRARY_DIRS}
    ${LIBCOCAINE_LIBRARY_DIRS}
    ${LIBMSGPACK_LIBRARY_DIRS})

SET(PLUGINS
    cache
    chrono
    docker
    elasticsearch
    graphite
    ipvs
    mongo
    node
    unicorn
    urlfetch)

FOREACH(PLUGIN ${PLUGINS})
    STRING(TOUPPER ${PLUGIN} OPTION_NAME)
    IF(${${OPTION_NAME}})
        ADD_SUBDIRECTORY(${PLUGIN})
    ENDIF()
ENDFOREACH(PLUGIN)
