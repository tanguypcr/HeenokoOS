cmake_minimum_required(VERSION 3.25)
project(HeenokoOS C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# Processus Écran : terminal passif lisant la FIFO.
add_executable(ecran ecran.c)

# Processus Shell : invite interactive, lance le noyau via fork()/execvp().
add_executable(monbash monbash.c parser.c)

# Processus Noyau : exécute les requêtes sur le SGF (data.bin).
add_executable(noyau noyau.c sgf_core.c disk.c)

# monbash cherche le noyau à côté de son propre exécutable.
add_dependencies(monbash noyau)
