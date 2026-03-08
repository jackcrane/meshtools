function(meshtools_set_project_warnings target warnings_as_errors)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
        )
    elseif(MSVC)
        target_compile_options(${target} INTERFACE /W4 /permissive-)
    endif()

    if(warnings_as_errors)
        if(MSVC)
            target_compile_options(${target} INTERFACE /WX)
        else()
            target_compile_options(${target} INTERFACE -Werror)
        endif()
    endif()
endfunction()

