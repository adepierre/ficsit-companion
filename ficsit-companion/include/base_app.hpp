#pragma once

#include <chrono>

class BaseApp
{
public:
    BaseApp();
    virtual ~BaseApp();
    void Render();

    virtual void SaveSession() = 0;

    bool HasRecentInteraction() const;

protected:
    virtual void RenderImpl() = 0;

private:
    std::chrono::steady_clock::time_point last_time_interacted;
};
