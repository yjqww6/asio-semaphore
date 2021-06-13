cmake_minimum_required(VERSION 3.5)

project(asio_semaphore)

find_package(Boost REQUIRED COMPONENTS system)

add_executable(main main.cpp semaphore.hpp details/nullary_op.hpp)
target_compile_features(main PRIVATE cxx_std_20)
target_link_libraries(main PRIVATE Boost::system)
