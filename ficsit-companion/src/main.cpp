#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h> // emscripten_set_beforeunload_callback
#else
// STB_IMAGE_IMPLEMENTATION is already defined in utils.cpp
#include "stb_image.h"
#if NDEBUG && defined(_WIN32)
#include <windows.h>
#endif
#endif

#include "app.hpp"

bool Render(SDL_Window* window, App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
#if defined(__EMSCRIPTEN__)
        // Hack to manually add input chars because numpad keys don't seem to trigger SDL_EVENT::SDL_TEXTINPUT with emscripten (??)
        // see similar issue here https://github.com/pthom/hello_imgui/issues/114
        // It may be a windows only issue though, so this hack *may* break stuff for other OS ?
        if (event.type == SDL_KEYDOWN)
        {
            switch (event.key.keysym.sym)
            {
            case SDLK_KP_0:
            {
                const char c = '0';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_1:
            case SDLK_KP_2:
            case SDLK_KP_3:
            case SDLK_KP_4:
            case SDLK_KP_5:
            case SDLK_KP_6:
            case SDLK_KP_7:
            case SDLK_KP_8:
            case SDLK_KP_9:
            {
                const char c = '1' + static_cast<char>(event.key.keysym.sym - SDLK_KP_1);
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_PERIOD:
            {
                const char c = '.';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_DIVIDE:
            {
                const char c = '/';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_MULTIPLY:
            {
                const char c = '*';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_MINUS:
            {
                const char c = '-';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            case SDLK_KP_PLUS:
            {
                const char c = '+';
                ImGui::GetIO().AddInputCharactersUTF8(&c);
                break;
            }
            default:
                break;
            }
        }
#endif
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)
            )
        )
        {

#if defined(__EMSCRIPTEN__)
            emscripten_cancel_main_loop();
#endif
            return false;
        }
    }

    // Init imgui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Ficsit Companion", NULL,
        ImGuiWindowFlags_NoNavInputs |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    app->Render();

    ImGui::End();

    // Render ImGui
    ImGui::Render();
    glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    return true;
}

int main(int argc, char* argv[])
{
#if NDEBUG && defined(_WIN32)
    // Hide console on Windows except if asked not to from the command line
    const std::vector<std::string> args(argv, argv + argc);
    HWND console = GetConsoleWindow();
    if (std::find(args.begin(), args.end(), "--show-console") == args.end())
    {
        ShowWindow(console, SW_HIDE);
    }
    else
    {
        ShowWindow(console, SW_SHOWDEFAULT);
    }
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        printf("Error %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_Window* window = SDL_CreateWindow(
        "Ficsit Companion",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1600, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s", SDL_GetError());
        return -1;
    }

#if !defined(__EMSCRIPTEN__)
    // Set window icon
    int width, height, channels;
    unsigned char* img = stbi_load("icon.png", &width, &height, &channels, 4);
    if (img == nullptr)
    {
        printf("Warning, error loading window icon");
    }
    SDL_Surface* icon = nullptr;
    if (img != nullptr)
    {
        icon = SDL_CreateRGBSurfaceWithFormatFrom(img, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
        if (icon == nullptr)
        {
            printf("Warning, error creating window icon");
        }
        else
        {
            SDL_SetWindowIcon(window, icon);
        }
    }
#endif

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // imgui: setup context
    // ---------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // Don't save window layout in ini file
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;//Enable Keyboard Navigation
    
    // Style
    ImGui::StyleColorsDark();

    // Setup platform/renderer
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app;
#if !defined(__EMSCRIPTEN__)
    while (Render(window, &app))
    {

    }
#else
    // Write to localStorage when quitting
    emscripten_set_beforeunload_callback(static_cast<void*>(&app), [](int event_type, const void* reserved, void* user_data) {
        // Save current session to disk
        static_cast<App*>(user_data)->SaveSession();
        // return empty string does not trigger the popup asking if we *really* want to quit
        return "";
    });

    struct WindowApp { SDL_Window* window; App* app; };
    WindowApp arg{ window, &app };
    emscripten_set_main_loop_arg([](void* arg) {
        WindowApp* window_app = static_cast<WindowApp*>(arg);
        Render(window_app->window, window_app->app);
    }, &arg, 0, true);
#endif

    // ImGui cleaning
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

#if !defined(__EMSCRIPTEN__)
    SDL_FreeSurface(icon);
    if (img != nullptr)
    {
        stbi_image_free(img);
    }
#endif
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
