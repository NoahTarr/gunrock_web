cmake_minimum_required(VERSION 3.0.0)
project(gunrock_web VERSION 0.1.0)

include(CTest)
enable_testing()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

add_executable(gunrock_web gunrock.cpp dthread.cpp FileService.cpp http_parser.c 
HTTP.cpp HTTPRequest.cpp HTTPResponse.cpp 
HttpService.cpp HttpUtils.cpp MyServerSocket.cpp MySocket.cpp)

target_link_libraries(gunrock_web  ${CMAKE_THREAD_LIBS_INIT})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
