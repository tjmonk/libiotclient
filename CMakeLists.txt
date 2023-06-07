cmake_minimum_required(VERSION 3.10)

# set the project name
project(iotclient
	VERSION 0.1
	DESCRIPTION "IOTHub Interface Functions"
)

include(GNUInstallDirs)

#add the library
add_library( ${PROJECT_NAME} SHARED
	src/iotclient.c
)

set_target_properties( ${PROJECT_NAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION 1
)

target_link_libraries( ${PROJECT_NAME} rt )

set(IOTCLIENT_HEADERS
    inc/iotclient.h
)

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${IOTCLIENT_HEADERS}")

target_include_directories( ${PROJECT_NAME} PRIVATE . )

target_include_directories( ${PROJECT_NAME} PUBLIC inc )

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iotclient)
