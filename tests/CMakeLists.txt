add_executable(city_tests)
list(APPEND ALL_TARGETS city_tests)
set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)

target_sources(city_tests
    PRIVATE
    test_main.cpp
)

target_include_directories(city_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(city_tests PRIVATE BUILD_TEST=1)

target_link_libraries(city_tests PRIVATE glm::glm)

enable_testing()
add_test(NAME city_tests COMMAND city_tests)

add_executable(utm_tests utility/utm_test_main.cpp)
target_include_directories(utm_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME utm_tests COMMAND utm_tests)
