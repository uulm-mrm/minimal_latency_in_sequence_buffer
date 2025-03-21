cmake_minimum_required(VERSION 3.10)
project(minimal_latency_buffer)

##################
## Dependencies ##
##################

# TODO: add boost?!
find_package(aduulm_cmake_tools REQUIRED)

##############
## Building ##
##############

# default paths for installing
set(INCLUDE_INSTALL_DIR include/ CACHE STRING "Path to installed include directory")
set(LIB_INSTALL_DIR lib/ CACHE STRING "Path to installed lib directory")
set(BIN_INSTALL_DIR bin/ CACHE STRING "Path to installed bin directory")
set(SHARE_INSTALL_DIR share/ CACHE STRING "Path to installed share directory")

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

################
## Installing ##
################

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION 0.1
        COMPATIBILITY AnyNewerVersion
)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR}
        LIBRARY DESTINATION ${LIB_INSTALL_DIR} COMPONENT Runtime
        ARCHIVE DESTINATION ${LIB_INSTALL_DIR} COMPONENT Development
        RUNTIME DESTINATION ${BIN_INSTALL_DIR} COMPONENT Runtime
        PUBLIC_HEADER DESTINATION ${INCLUDE_INSTALL_DIR} COMPONENT Development
        BUNDLE DESTINATION ${BIN_INSTALL_DIR} COMPONENT Runtime
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/config.cmake.in"
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
        PATH_VARS INCLUDE_INSTALL_DIR LIB_INSTALL_DIR BIN_INSTALL_DIR
)

install(EXPORT ${PROJECT_NAME}Targets DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME} NAMESPACE ${PROJECT_NAME}::)
install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${PROJECT_NAME})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION ${INCLUDE_INSTALL_DIR})

#####################
## Python Bindings ##
#####################
# IMPORTANT! install python code first as long as there is code inside the bindings __init__.py file

aduulm_install_ros2_python_code( minimal_latency_buffer PACKAGE_DIR ${PROJECT_SOURCE_DIR}/python/)
add_subdirectory(src/bindings)


###########
## Tests ##
###########
add_subdirectory(test/unittests)
