add_executable(salal_sandbox
    main.cpp
)

target_link_libraries(salal_sandbox
    PRIVATE
        salal::app
)

set_target_properties(salal_sandbox PROPERTIES OUTPUT_NAME sandbox FOLDER salal)
