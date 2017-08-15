# require at least CMake version 3.0.2
cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

# create the project
project(
  ezgl
  VERSION 0.0.1
  LANGUAGES CXX
)

# we rely on GTK2 for the GUI, so make sure the system has it
find_package(GTK2 REQUIRED)
