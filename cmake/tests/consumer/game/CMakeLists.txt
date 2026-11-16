add_executable(consumer_game main.cpp)
salal_windows_manifest(consumer_game)

get_target_property(sources consumer_game SOURCES)
list(FILTER sources INCLUDE REGEX "dpi-aware\.manifest$")
if(NOT sources OR NOT EXISTS "${sources}")
    message(FATAL_ERROR "salal_windows_manifest called from a consumer's directory added \"${sources}\", not salal's manifest")
endif()
