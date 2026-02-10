cmake_minimum_required(VERSION 3.22)

add_executable(bsa_packer_tests)
mo2_configure_tests(bsa_packer_tests WARNINGS OFF)
target_link_libraries(bsa_packer_tests PRIVATE bsa_packer)
