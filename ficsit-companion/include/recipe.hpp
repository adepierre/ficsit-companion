#pragma once

#include "fractional_number.hpp"

#include <string>
#include <vector>

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
    Recipe(const std::vector<CountedItem>& ins, const std::vector<CountedItem>& outs, const std::string& machine, const bool alternate, const std::string& name = "");

    bool MatchName(const std::string& s) const;
    bool MatchIngredients(const std::string& s) const;

    const std::string name;
    const std::vector<CountedItem> ins;
    const std::vector<CountedItem> outs;
    const std::string machine;
    const bool alternate;

private:
    std::string lower_name;
    std::vector<std::string> lower_ingredients;
};
