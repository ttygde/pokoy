cmake_minimum_required(VERSION 3.0)
project(pokoy)
set(VERSION "0.2.3")

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_FIND_MODULE_DIR})

# build pokoy binary
find_package(XCB REQUIRED COMPONENTS XCB KEYSYMS ICCCM SCREENSAVER)
set(C_STANDARD 11)
set(C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    pokoy.c
    )
# Strip binary for release builds
if (CMAKE_BUILD_TYPE STREQUAL Release)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} ${PROJECT_NAME})
endif ()
target_link_libraries(${PROJECT_NAME}
    XCB::XCB
    XCB::KEYSYMS
    XCB::ICCCM
    XCB::SCREENSAVER
    )

# build manpage
find_program(A2X_EXECUTABLE NAMES a2x)
set(A2X_OPTIONS
    --doctype manpage
    --format manpage
    --attribute revnumber=${VERSION}
    --destination-dir=${CMAKE_BINARY_DIR}
    )
set(MAN_FILE pokoy.1)
set(MAN_FILE_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/${MAN_FILE}.txt)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/${MAN_FILE}
    COMMAND ${A2X_EXECUTABLE} ${A2X_OPTIONS} ${MAN_FILE_INPUT}
    DEPENDS ${MAN_FILE_INPUT}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
add_custom_target(manpage ALL DEPENDS ${MAN_FILE})

# install all
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${MAN_FILE} DESTINATION share/man/man1)
install(TARGETS ${PROJECT_NAME})
