#include "recipe.hpp"
#include "utils.hpp"

#include <algorithm>

std::string SpaceToNewLine(const std::string& s)
{
    std::string output = s;
    std::replace(output.begin(), output.end(), ' ', '\n');
    return output;
}

Item::Item(const std::string& name, const std::string& icon_path) :
    name(name),
    new_line_name(SpaceToNewLine(name)),
    icon_gl_index(LoadTextureFromFile(icon_path))
{

}

Recipe::Recipe(const std::vector<CountedItem>& ins, const std::vector<CountedItem>& outs, const std::string& machine, const bool alternate, const std::string& name) :
    ins(ins), outs(outs), machine(machine), alternate(alternate), name(name)
{
    lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });

    lower_ingredients.reserve(ins.size() + outs.size());
    for (const auto& i : ins)
    {
        lower_ingredients.push_back(i.item->name);
    }
    for (const auto& o : outs)
    {
        lower_ingredients.push_back(o.item->name);
    }

    for (auto& s : lower_ingredients)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    }
}

size_t Recipe::FindInName(const std::string& s) const
{
    std::string lower_s = s;
    std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), [](unsigned char c) { return std::tolower(c); });

    return lower_name.find(lower_s);
}

size_t Recipe::FindInIngredients(const std::string& s) const
{
    std::string lower_s = s;
    std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), [](unsigned char c) { return std::tolower(c); });

    size_t min_pos = std::string::npos;
    for (const auto& s : lower_ingredients)
    {
        min_pos = std::min(min_pos, s.find(lower_s));
    }

    return min_pos;
}
