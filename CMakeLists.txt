cmake_minimum_required(VERSION 3.17)
project(BeastWebChattserver)

set(CMAKE_CXX_STANDARD 17)
FIND_PACKAGE(Boost 1.42.0 REQUIRED COMPONENTS system thread regex)
IF(Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
    LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF(Boost_FOUND)

SET(USED_LIBS ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_REGEX_LIBRARY})

add_executable(beastclientexample main.cpp chat_message.hpp)
TARGET_LINK_LIBRARIES(beastclientexample ${USED_LIBS})