add_executable(city_tests)

target_sources(city_tests
    PRIVATE
    test_main.cpp
    base/test_strings.cpp
)

target_include_directories(city_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(city_tests PRIVATE BUILD_TEST=1)

target_link_libraries(city_tests PRIVATE glm::glm)

enable_testing()
add_test(NAME city_tests COMMAND city_tests)
