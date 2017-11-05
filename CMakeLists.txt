cmake_minimum_required(VERSION 3.0)
project(yael)

include(GNUInstallDirs)
find_package(Glog REQUIRED)

add_library(yael
    include/yael/network/Address.h
    include/yael/network/Socket.h
    include/yael/EventListener.h
    include/yael/EventLoop.h
    include/yael/NetworkSocketListener.h
    include/yael/SocketListener.h
    include/yael/yael.h
    src/network/Address.cpp
    src/network/Socket.cpp
    src/EventLoop.cpp
    src/NetworkSocketListener.cpp
)
set_property(TARGET yael PROPERTY CXX_STANDARD 17)
target_link_libraries(yael glog::glog)
target_include_directories(yael PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

add_executable(test-peerdb-bug test/peerdb_bug.cpp)
set_property(TARGET test-peerdb-bug PROPERTY CXX_STANDARD 17)
target_link_libraries(test-peerdb-bug yael)

add_executable(test-many-clients test/many_clients.cpp)
set_property(TARGET test-many-clients PROPERTY CXX_STANDARD 17)
target_link_libraries(test-many-clients yael)

install(TARGETS yael EXPORT yael-config
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT yael-config
    FILE yael-config.cmake
    NAMESPACE yael::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yael)
