#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

struct Building;
struct Item;
struct Recipe;

namespace Data
{
    /// @brief Load data (recipes, items...) for a given game
    /// @param game The game name to load (it should match an existing game.json data file)
    void LoadData(const std::string& game);

    /// @brief Get the version of the loaded data
    const std::string& Version();

    /// @brief Get all known items
    const std::unordered_map<std::string, std::unique_ptr<Item>>& Items();

    /// @brief Get all known buildings
    const std::unordered_map<std::string, std::unique_ptr<Building>>& Buildings();

    /// @brief Get all known recipes
    const std::vector<std::unique_ptr<Recipe>>& Recipes();
}
