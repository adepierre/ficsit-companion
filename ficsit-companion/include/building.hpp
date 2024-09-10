#pragma once

#include "fractional_number.hpp"

#include <string>

struct Building
{
    Building(const std::string& name, const FractionalNumber& somersloop_mult);

    const std::string name;
    const FractionalNumber somersloop_mult;
};
