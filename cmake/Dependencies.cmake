include(FetchContent)

function(meshtools_configure_glfw)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    if(MESHTOOLS_GLFW_SOURCE_DIR)
        add_subdirectory("${MESHTOOLS_GLFW_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/glfw-build" EXCLUDE_FROM_ALL)
        return()
    endif()

    if(NOT MESHTOOLS_USE_FETCHCONTENT)
        message(FATAL_ERROR "GLFW was not provided. Set MESHTOOLS_GLFW_SOURCE_DIR or enable MESHTOOLS_USE_FETCHCONTENT.")
    endif()

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
endfunction()

function(meshtools_configure_imgui)
    if(MESHTOOLS_IMGUI_SOURCE_DIR)
        set(imgui_root "${MESHTOOLS_IMGUI_SOURCE_DIR}")
    else()
        if(NOT MESHTOOLS_USE_FETCHCONTENT)
            message(FATAL_ERROR "Dear ImGui was not provided. Set MESHTOOLS_IMGUI_SOURCE_DIR or enable MESHTOOLS_USE_FETCHCONTENT.")
        endif()

        FetchContent_Declare(
            imgui_src
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG docking
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(imgui_src)
        set(imgui_root "${imgui_src_SOURCE_DIR}")
    endif()

    add_library(imgui STATIC
        "${imgui_root}/imgui.cpp"
        "${imgui_root}/imgui_demo.cpp"
        "${imgui_root}/imgui_draw.cpp"
        "${imgui_root}/imgui_tables.cpp"
        "${imgui_root}/imgui_widgets.cpp"
        "${imgui_root}/backends/imgui_impl_glfw.cpp"
        "${imgui_root}/backends/imgui_impl_opengl3.cpp"
    )

    target_include_directories(imgui
        PUBLIC
            "${imgui_root}"
            "${imgui_root}/backends"
    )

    target_link_libraries(imgui PUBLIC glfw)
    target_compile_features(imgui PUBLIC cxx_std_20)
endfunction()

function(meshtools_configure_dependencies)
    meshtools_configure_glfw()
    meshtools_configure_imgui()
endfunction()
