#pragma once

#include "fractional_number.hpp"

#include <string>
#include <vector>

struct Building;

struct Item
{
    Item(const std::string& name, const std::string& icon_path);
    const std::string name;
    const std::string new_line_name;
    const unsigned int icon_gl_index;
};

struct CountedItem
{
    CountedItem(const Item* item, const FractionalNumber& i): item(item), quantity(i) {  }
    const Item* item;
    FractionalNumber quantity;
};

struct Recipe
{
    Recipe(
        const std::vector<CountedItem>& ins,
        const std::vector<CountedItem>& outs,
        const Building* building,
        const bool alternate,
        const double power,
        const std::string& name = "",
        const bool is_spoiler = false
    );

    /// @brief Search for a string in this recipe name, case insensitive
    /// @param s String to search
    /// @return The position the string was found in this recipe name, or std::string::npos if not found
    size_t FindInName(const std::string& s) const;

    /// @brief Search for a string in this recipe ingredients, case insensitive
    /// @param s String to search
    /// @return The position the string was found in this recipe ingredients name, or std::string::npos if not found
    size_t FindInIngredients(const std::string& s) const;

    void Render(const bool render_name = true, const bool render_items_icons = true) const;

    const std::string name;
    const std::string display_name;
    const std::vector<CountedItem> ins;
    const std::vector<CountedItem> outs;
    const Building* building;
    const bool alternate;
    const bool is_spoiler;
    const double power;

private:
    std::string lower_name;
    std::vector<std::string> lower_ingredients;
};
