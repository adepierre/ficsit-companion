#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "recipe.hpp"

namespace Data
{
    /// @brief Load data (recipes, items...) for a given game
    /// @param game The game name to load (it should match an existing game.json data file)
    void LoadData(const std::string& game);

    /// @brief Get the version of the loaded data
    const std::string& Version();

    /// @brief Get all known items
    const std::unordered_map<std::string, std::unique_ptr<Item>>& Items();

    /// @brief Get all known recipes
    const std::vector<Recipe>& Recipes();
}
