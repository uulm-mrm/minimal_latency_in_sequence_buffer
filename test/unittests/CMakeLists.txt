find_package(GTest REQUIRED)

if(NOT ${GTEST_FOUND})
  message(WARNING "GTest not found, skipping tests for measurement buffer")
  return()
endif()
enable_testing()

# executable
SET(TEST_NAME minimal_latency_buffer_tests)

add_executable(${TEST_NAME}
        helper_functions.cpp
        estimator.cpp
        minimal_latency_buffer/single_sensor.cpp
        minimal_latency_buffer/two_sensors.cpp
        fixed_lag_buffer/single_sensor.cpp
        fixed_lag_buffer/two_sensors.cpp
)

target_compile_features(${TEST_NAME} PUBLIC cxx_std_20)

if (${COVERAGE})
  message(WARNING "COVERAGE IS ON")
  target_link_libraries(${TEST_NAME}
          PRIVATE
          -fprofile-arcs -ftest-coverage
  )
  target_compile_options(${TEST_NAME} PRIVATE --coverage -g -O0)
endif()

target_link_libraries( ${TEST_NAME}
        PRIVATE
        GTest::gtest_main
        minimal_latency_buffer::minimal_latency_buffer
)
gtest_discover_tests(${TEST_NAME})

# Might be removed later on since the dependency on the ROS eco-system is usually not desired within library code.
# However, at the moment this allows executing the above test via colcon. Is is suggested to add the console handler in
# order to view the full output: 'colcon test --packages-select measurement_buffer --event-handlers console_cohesion+'
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest_test(${TEST_NAME})
endif()