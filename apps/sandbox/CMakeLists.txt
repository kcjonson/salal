add_executable(salal_sandbox
    main.cpp
)

target_link_libraries(salal_sandbox
    PRIVATE
        salal::app
)

# The base shader with no hooks, as a game with none gets it.
salal_base_shader(salal_sandbox)

set_target_properties(salal_sandbox PROPERTIES OUTPUT_NAME sandbox FOLDER salal)
