# build python bindings if nanobind is available (this requires at least python 3.8)
find_package(Python 3.8 COMPONENTS Interpreter Development.Module QUIET)

if (Python_FOUND)
    # detect the installed nanobind package and import it into CMake
    execute_process(
            COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
    find_package(nanobind CONFIG QUIET)

    set(PY_TARGET_NAME "_minimal_latency_buffer")

    set(MODULE_NAME "minimal_latency_buffer")

    # collect files of this module
    set(SOURCE_FILES
            bindings.cpp
            types.cpp
            minimal_latency_buffer.cpp
            fixed_lag_buffer.cpp
    )

    # builds the python bindings module
    nanobind_add_module(${PY_TARGET_NAME} STABLE_ABI ${SOURCE_FILES})
    foreach (nanobind_name nanobind nanobind-static nanobind-abi3)
        if (TARGET ${nanobind_name})
	    target_compile_options(${nanobind_name} PRIVATE -Wno-shadow -Wno-pedantic)
        endif()
    endforeach ()

    target_compile_features(${PY_TARGET_NAME} PRIVATE cxx_std_17)

    # suppress some warnings within the compilation of the bindings
    target_compile_options(${PY_TARGET_NAME} PRIVATE -Wno-shadow -Wno-pedantic)

    target_link_libraries(${PY_TARGET_NAME}
            PRIVATE
            ${PROJECT_NAME}::${PROJECT_NAME}
    )

    install(TARGETS ${PY_TARGET_NAME}
            LIBRARY DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME} COMPONENT Runtime
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/__init__.py
            DESTINATION ${PYTHON_INSTALL_DIR}/${MODULE_NAME})

else()
    message(WARNING "Nanobind not found, skipping build of python bindings for the minimal latency buffer")
endif()
