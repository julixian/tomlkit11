cmake_minimum_required(VERSION 3.14)

project(tomlkit11 VERSION 1.0.0 LANGUAGES CXX)

if(DEFINED PROJECT_IS_TOP_LEVEL)
    set(TOMLKIT11_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(TOMLKIT11_IS_TOP_LEVEL ON)
    else()
        set(TOMLKIT11_IS_TOP_LEVEL OFF)
    endif()
endif()

option(TOMLKIT11_BUILD_TESTS "Build tomlkit11 unit tests" ${TOMLKIT11_IS_TOP_LEVEL})
option(TOMLKIT11_BUILD_TOOLS "Build tomlkit11 tools and compliance adapter" ${TOMLKIT11_IS_TOP_LEVEL})
option(TOMLKIT11_USE_SINGLE_INCLUDE "Use the amalgamated single_include header for tests and tools" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tomlkit11 INTERFACE)
add_library(tomlkit11::tomlkit11 ALIAS tomlkit11)

target_compile_features(tomlkit11 INTERFACE cxx_std_11)

if(TOMLKIT11_USE_SINGLE_INCLUDE)
    set(TOMLKIT11_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/single_include")
else()
    set(TOMLKIT11_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

target_include_directories(tomlkit11 INTERFACE
    $<BUILD_INTERFACE:${TOMLKIT11_BUILD_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

function(tomlkit11_apply_common_flags target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8)
    else()
        target_compile_options(${target_name} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
endfunction()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tomlkit11.cpp")
    add_library(tomlkit11_compiled STATIC EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tomlkit11.cpp"
    )
    add_library(tomlkit11::tomlkit11_compiled ALIAS tomlkit11_compiled)
    target_compile_features(tomlkit11_compiled PUBLIC cxx_std_11)
    target_compile_definitions(tomlkit11_compiled PUBLIC TOMLKIT11_COMPILED_LIB)
    target_include_directories(tomlkit11_compiled PUBLIC
        $<BUILD_INTERFACE:${TOMLKIT11_BUILD_INCLUDE_DIR}>
    )
    set_target_properties(tomlkit11_compiled PROPERTIES POSITION_INDEPENDENT_CODE ON)
    tomlkit11_apply_common_flags(tomlkit11_compiled)
endif()

if(TOMLKIT11_BUILD_TESTS)
    enable_testing()

    set(_tomlkit11_standard_tests
        test_lexer_utf8
        test_v1_1_features
        test_roundtrip
        test_surgical_edit
        test_ergonomic_api
        test_api_edge_cases
    )

    foreach(_test_name IN LISTS _tomlkit11_standard_tests)
        set(_test_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_test_name}.cpp")
        if(EXISTS "${_test_src}")
            add_executable(${_test_name} "${_test_src}")
            target_link_libraries(${_test_name} PRIVATE tomlkit11::tomlkit11)
            tomlkit11_apply_common_flags(${_test_name})
            add_test(NAME ${_test_name} COMMAND ${_test_name})
        endif()
    endforeach()

    set(_no_ex_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_no_exceptions.cpp")
    if(EXISTS "${_no_ex_src}")
        add_executable(test_no_exceptions "${_no_ex_src}")
        target_link_libraries(test_no_exceptions PRIVATE tomlkit11::tomlkit11)
        tomlkit11_apply_common_flags(test_no_exceptions)
        if(MSVC)
            target_compile_options(test_no_exceptions PRIVATE /EHs-c-)
            target_compile_definitions(test_no_exceptions PRIVATE _HAS_EXCEPTIONS=0)
        else()
            target_compile_options(test_no_exceptions PRIVATE -fno-exceptions)
            if(CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
                target_compile_definitions(test_no_exceptions PRIVATE _HAS_EXCEPTIONS=0)
            endif()
        endif()
        add_test(NAME test_no_exceptions COMMAND test_no_exceptions)
    endif()

    set(_compiled_impl_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compiled_lib/lib_impl.cpp")
    set(_compiled_main_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compiled_lib/lib_main.cpp")
    if(EXISTS "${_compiled_impl_src}" AND EXISTS "${_compiled_main_src}")
        add_executable(test_compiled_lib
            "${_compiled_impl_src}"
            "${_compiled_main_src}"
        )
        target_link_libraries(test_compiled_lib PRIVATE tomlkit11::tomlkit11)
        target_compile_definitions(test_compiled_lib PRIVATE TOMLKIT11_COMPILED_LIB)
        tomlkit11_apply_common_flags(test_compiled_lib)
        add_test(NAME test_compiled_lib COMMAND test_compiled_lib)
    endif()

    set(_compiled_target_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compiled_lib/target_main.cpp")
    if(TARGET tomlkit11_compiled AND EXISTS "${_compiled_target_src}")
        add_executable(test_compiled_target "${_compiled_target_src}")
        target_link_libraries(test_compiled_target PRIVATE tomlkit11::tomlkit11_compiled)
        tomlkit11_apply_common_flags(test_compiled_target)
        add_test(NAME test_compiled_target COMMAND test_compiled_target)
    endif()
endif()

if(TOMLKIT11_BUILD_TOOLS)
    set(_adapter_dir "${CMAKE_CURRENT_SOURCE_DIR}/tools/toml-test-adapter")

    if(EXISTS "${_adapter_dir}/DecoderMain.cpp")
        add_executable(tomlkit11-decoder "${_adapter_dir}/DecoderMain.cpp")
        target_include_directories(tomlkit11-decoder PRIVATE "${_adapter_dir}")
        target_link_libraries(tomlkit11-decoder PRIVATE tomlkit11::tomlkit11)
        tomlkit11_apply_common_flags(tomlkit11-decoder)
    endif()

    if(EXISTS "${_adapter_dir}/EncoderMain.cpp")
        add_executable(tomlkit11-encoder "${_adapter_dir}/EncoderMain.cpp")
        target_include_directories(tomlkit11-encoder PRIVATE "${_adapter_dir}")
        target_link_libraries(tomlkit11-encoder PRIVATE tomlkit11::tomlkit11)
        tomlkit11_apply_common_flags(tomlkit11-encoder)
    endif()

    if(EXISTS "${_adapter_dir}/ComplianceRunnerMain.cpp")
        add_executable(tomlkit11-compliance-runner "${_adapter_dir}/ComplianceRunnerMain.cpp")
        target_include_directories(tomlkit11-compliance-runner PRIVATE "${_adapter_dir}")
        target_link_libraries(tomlkit11-compliance-runner PRIVATE tomlkit11::tomlkit11)
        tomlkit11_apply_common_flags(tomlkit11-compliance-runner)
    endif()
endif()

install(TARGETS tomlkit11 EXPORT tomlkit11Targets)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/tomlkit11"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT tomlkit11Targets
    FILE tomlkit11Targets.cmake
    NAMESPACE tomlkit11::
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/tomlkit11"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tomlkit11Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tomlkit11Config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/tomlkit11"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tomlkit11ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tomlkit11Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tomlkit11ConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/tomlkit11"
)
