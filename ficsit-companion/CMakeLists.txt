project(ficsit-companion)

set(HEADER_FILES
	include/base_app.hpp
	include/building.hpp
	include/fractional_number.hpp
	include/game_data.hpp
	include/json.hpp
	include/link.hpp
	include/node.hpp
	include/pin.hpp
	include/production_app.hpp
	include/recipe.hpp
	include/utils.hpp
)

set(SOURCE_FILES
    ${imgui_SOURCE}

    src/base_app.cpp
    src/building.cpp
    src/fractional_number.cpp
    src/game_data.cpp
    src/json.cpp
    src/link.cpp
    src/node.cpp
    src/pin.cpp
    src/production_app.cpp
    src/recipe.cpp
    src/utils.cpp

    src/main.cpp
)
source_group(ImGui FILES ${imgui_SOURCE})

add_executable(${PROJECT_NAME} ${HEADER_FILES} ${SOURCE_FILES})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_INCLUDE_FOLDERS})

if (NOT DEFINED EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static)

    # Copy assets next to output build
    add_custom_command(
      TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../assets $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION .)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../assets/" DESTINATION .)
else()
    # Make emscripten outputs html file instead of default js and redirect all emscripten generated files to a dedicated web folder
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../web"
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        "-Os"
        "--use-port=sdl2"
        "-sDISABLE_EXCEPTION_CATCHING=0"
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Os"
        "--use-port=sdl2"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_EXIT_RUNTIME=0"
        "-sDISABLE_EXCEPTION_CATCHING=0"
        "-sASYNCIFY" # TODO: switch to JSPI once there is better support
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/../assets@/"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/../emscripten/shell_index.html"
        "-sMINIFY_HTML=0"
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/index.html
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../assets/icon.png $<TARGET_FILE_DIR:${PROJECT_NAME}>/icon.png
    )
endif()
