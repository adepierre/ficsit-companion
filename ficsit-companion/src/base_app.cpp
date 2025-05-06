#include "base_app.hpp"

#include <imgui.h>

BaseApp::BaseApp()
{
    last_time_interacted = std::chrono::steady_clock::now();
}

BaseApp::~BaseApp()
{

}

void BaseApp::Render()
{
    RenderImpl();

    // If any user input happened, reset last time interaction
    const ImGuiIO& io = ImGui::GetIO();
    for (const auto& k : io.KeysData)
    {
        if (k.Down)
        {
            last_time_interacted = std::chrono::steady_clock::now();
            break;
        }
    }

    if (ImGui::IsAnyMouseDown() || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
    {
        last_time_interacted = std::chrono::steady_clock::now();
    }
}

bool BaseApp::HasRecentInteraction() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_time_interacted).count() < 10000;
}
