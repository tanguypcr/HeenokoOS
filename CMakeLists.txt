cmake_minimum_required(VERSION 3.25)
project(HeenokoOS C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

add_executable(ecran ecran.c)

add_executable(monbash monbash.c parser.c)

add_executable(noyau noyau.c sgf_core.c disk.c)

add_dependencies(monbash noyau)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
