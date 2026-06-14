#include "building.hpp"

Building::Building(
    const std::string& name,
    const FractionalNumber& somersloop_mult,
    const double power,
    const double power_exponent,
    const double somersloop_power_exponent,
    const bool variable_power,
    const bool production_multiplied) :
    name(name),
    somersloop_mult(somersloop_mult),
    power(power),
    power_exponent(power_exponent),
    somersloop_power_exponent(somersloop_power_exponent),
    variable_power(variable_power),
    production_multiplied(production_multiplied)
{

}
