cmake_minimum_required(VERSION 3.16)
project(pqc_kyber LANGUAGES C CXX)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(OpenSSL REQUIRED)

set(KYBER_K "3" CACHE STRING "Kyber parameter: 2 (512), 3 (768), 4 (1024)")
set_property(CACHE KYBER_K PROPERTY STRINGS 2 3 4)
if(NOT KYBER_K MATCHES "^[234]$")
  message(FATAL_ERROR "KYBER_K must be 2, 3 or 4")
endif()

get_filename_component(KYBER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(kyber_chaotic STATIC
  ${KYBER_ROOT}/ref/kem.c
  ${KYBER_ROOT}/ref/indcpa.c
  ${KYBER_ROOT}/ref/polyvec.c
  ${KYBER_ROOT}/ref/poly.c
  ${KYBER_ROOT}/ref/ntt.c
  ${KYBER_ROOT}/ref/cbd.c
  ${KYBER_ROOT}/ref/reduce.c
  ${KYBER_ROOT}/ref/verify.c
  ${KYBER_ROOT}/ref/randombytes.c
  ${KYBER_ROOT}/ref/hashing.c
  ${KYBER_ROOT}/hash_options/chaotic_hash.c
)
set_target_properties(kyber_chaotic PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS YES
  POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(kyber_chaotic PUBLIC KYBER_K=${KYBER_K})
target_include_directories(kyber_chaotic PUBLIC
  $<BUILD_INTERFACE:${KYBER_ROOT}/ref>
  $<BUILD_INTERFACE:${KYBER_ROOT}/hash_options>
  $<INSTALL_INTERFACE:include/pqc_kyber/ref>
  $<INSTALL_INTERFACE:include/pqc_kyber/hash_options>
)

add_executable(kem_check src/kem_check.cpp)
target_compile_features(kem_check PRIVATE cxx_std_17)
target_link_libraries(kem_check PRIVATE kyber_chaotic)

foreach(node sender receiver)
  add_executable(${node} src/${node}.cpp)
  target_compile_features(${node} PRIVATE cxx_std_17)
  target_link_libraries(${node} PRIVATE kyber_chaotic OpenSSL::Crypto)
  ament_target_dependencies(${node} PUBLIC rclcpp std_msgs)
endforeach()

if(BUILD_TESTING)
  enable_testing()
  add_executable(demo_crypto_test test/demo_crypto_test.cpp)
  target_compile_features(demo_crypto_test PRIVATE cxx_std_17)
  target_include_directories(demo_crypto_test PRIVATE src)
  target_link_libraries(demo_crypto_test PRIVATE kyber_chaotic OpenSSL::Crypto)
  add_test(NAME demo_encryption_authentication COMMAND demo_crypto_test)
  add_test(NAME cpp_kem_chaotic_check COMMAND kem_check)
  add_executable(hash_regression ${KYBER_ROOT}/ref/test/test_hashing.c)
  target_link_libraries(hash_regression PRIVATE kyber_chaotic)
  add_test(NAME chaotic_hash_regression COMMAND hash_regression)
  add_executable(rejection_regression ${KYBER_ROOT}/ref/test/test_rejection.c)
  target_link_libraries(rejection_regression PRIVATE kyber_chaotic)
  add_test(NAME ciphertext_rejection_regression COMMAND rejection_regression)
endif()

install(TARGETS kyber_chaotic EXPORT kyber_chaotic_targets
  ARCHIVE DESTINATION lib
)
install(TARGETS kem_check sender receiver DESTINATION lib/${PROJECT_NAME})
# Preserve the relative include from ref/hashing.h to hash_options/chaotic_hash.h.
install(DIRECTORY ${KYBER_ROOT}/ref/ DESTINATION include/pqc_kyber/ref
  FILES_MATCHING PATTERN "*.h" PATTERN "test" EXCLUDE PATTERN "nistkat" EXCLUDE
)
install(DIRECTORY ${KYBER_ROOT}/hash_options/
  DESTINATION include/pqc_kyber/hash_options FILES_MATCHING PATTERN "*.h"
)
ament_export_targets(kyber_chaotic_targets HAS_LIBRARY_TARGET)
ament_package()
