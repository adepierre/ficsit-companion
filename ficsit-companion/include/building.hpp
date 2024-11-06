#pragma once

#include "fractional_number.hpp"

#include <string>

struct Building
{
    Building(const std::string& name, const FractionalNumber& somersloop_mult,
        const double power, const double power_exponent,
        const double somersloop_power_exponent, const bool variable_power
    );

    const std::string name;
    const FractionalNumber somersloop_mult;
    const double power;
    const double power_exponent;
    const double somersloop_power_exponent;
    const bool variable_power;
};
