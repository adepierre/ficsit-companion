include(FetchContent)

find_package(SDL2 QUIET)

if (NOT SDL2_FOUND)
    FetchContent_Declare(
        sdl2
        GIT_REPOSITORY    https://github.com/libsdl-org/SDL.git
        GIT_TAG           release-2.30.5
        GIT_SHALLOW       TRUE
        GIT_PROGRESS      TRUE
    )

    # Change the folder for better organization in Visual Studio
    set(CMAKE_FOLDER_BACKUP ${CMAKE_FOLDER})
    set(CMAKE_FOLDER SDL2)
    # Set option values for SDL
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sdl2)
    set(CMAKE_FOLDER ${CMAKE_FOLDER_BACKUP})
endif()
