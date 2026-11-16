# A game that brings salal in through FetchContent and calls its helpers from a directory of its own,
# where nothing salal set at its own directory scope reaches. Configuring this is the test.
cmake_minimum_required(VERSION 3.25)
project(salal_consumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(salal SOURCE_DIR "${SALAL_SOURCE_DIR}")
FetchContent_MakeAvailable(salal)

add_subdirectory(game)
