#include "fractional_number.hpp"

#include <iomanip>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>

FractionalNumber::FractionalNumber(const long long int n, const long long int d) : numerator(n), denominator(d)
{
    Simplify();
}

// Convert a string representation to an integer. Return the integer and a power of 10 multiplier
// "40"      --> (40,     0)
// "34.5"    --> (345,    1)
// "4.12345" --> (412345, 5)
std::pair<long long int, int> StringToInt(const std::string& s)
{
    const size_t point_index = s.find('.');
    if (point_index == std::string::npos)
    {
        return { std::stoll(s), 0 };
    }

    const long long int int_part = std::stoll(s.substr(0, point_index));
    const long long int decimal_part = std::stoll(s.substr(point_index + 1));

    long long int multiplier = 1;
    for (size_t i = 0; i < s.size() - point_index - 1; ++i)
    {
        multiplier *= 10;
    }

    return { int_part * multiplier + decimal_part, static_cast<int>(s.size() - point_index - 1) };
}

FractionalNumber::FractionalNumber(const std::string& s)
{
    const std::regex pattern("(\\d+(?:\\.\\d+)?)(?:/(\\d+(?:\\.\\d+)?))?");
    std::smatch matches;

    if (!std::regex_match(s, matches, pattern))
    {
        throw std::domain_error("Invalid input string");
    }

    auto [numerator_int, numerator_mult] = StringToInt(matches[1]);
    auto [denominator_int, denominator_mult] = matches[2].matched ? StringToInt(matches[2]) : std::make_pair(1LL, 0);

    for (int i = numerator_mult; i < std::max(numerator_mult, denominator_mult); ++i)
    {
        numerator_int *= 10;
    }
    for (int i = denominator_mult; i < std::max(numerator_mult, denominator_mult); ++i)
    {
        denominator_int *= 10;
    }

    numerator = numerator_int;
    denominator = denominator_int;
    Simplify();
}

long long int FractionalNumber::GetNumerator() const
{
    return numerator;
}

long long int FractionalNumber::GetDenominator() const
{
    return denominator;
}

double FractionalNumber::GetValue() const
{
    return value;
}

std::string& FractionalNumber::GetStringFraction()
{
    if (!str_fraction.has_value())
    {
        if (denominator == 1)
        {
            str_fraction = std::to_string(numerator);
        }
        else
        {
            str_fraction = std::to_string(numerator) + "/" + std::to_string(denominator);
        }
    }

    return str_fraction.value();
}

std::string& FractionalNumber::GetStringFloat()
{
    if (!str_float.has_value())
    {
        std::stringstream sstream;
        sstream << std::fixed << std::setprecision(3) << value;
        str_float = sstream.str();
    }
    return str_float.value();
}

FractionalNumber& FractionalNumber::operator*=(const FractionalNumber& rhs)
{
    numerator *= rhs.numerator;
    denominator *= rhs.denominator;
    Simplify();
    return *this;
}

FractionalNumber& FractionalNumber::operator/=(const FractionalNumber& rhs)
{
    numerator *= rhs.denominator;
    denominator *= rhs.numerator;
    Simplify();
    return *this;
}

FractionalNumber& FractionalNumber::operator+=(const FractionalNumber& rhs)
{
    numerator = numerator * rhs.denominator + rhs.numerator * denominator;
    denominator = denominator * rhs.denominator;
    Simplify();
    return *this;
}

FractionalNumber& FractionalNumber::operator-=(const FractionalNumber& rhs)
{
    numerator = numerator * rhs.denominator - rhs.numerator * denominator;
    denominator = denominator * rhs.denominator;
    Simplify();
    return *this;
}

bool FractionalNumber::operator!=(const FractionalNumber& other) const
{
    return numerator != other.numerator || denominator != other.denominator;
}

bool FractionalNumber::operator==(const FractionalNumber& other) const
{
    return numerator == other.numerator && denominator == other.denominator;
}

bool FractionalNumber::operator<(const FractionalNumber& other) const
{
    return numerator * other.denominator < other.numerator * denominator;
}

bool FractionalNumber::operator>(const FractionalNumber& other) const
{
    return numerator * other.denominator > other.numerator * denominator;
}

void FractionalNumber::Simplify()
{
    const long long int gcd = numerator == 0 ? denominator : std::gcd(numerator, denominator);
    numerator /= gcd;
    denominator /= gcd;
    UpdateValue();
}

void FractionalNumber::UpdateValue()
{
    value = static_cast<double>(numerator) / denominator;
    str_float.reset();
    str_fraction.reset();
}

FractionalNumber operator*(FractionalNumber lhs, const FractionalNumber& rhs)
{
    return lhs *= rhs;
}

FractionalNumber operator+(FractionalNumber lhs, const FractionalNumber& rhs)
{
    return lhs += rhs;
}

FractionalNumber operator-(FractionalNumber lhs, const FractionalNumber& rhs)
{
    return lhs -= rhs;
}

FractionalNumber operator/(const FractionalNumber& lhs, const FractionalNumber& rhs)
{
    return FractionalNumber(lhs.GetNumerator() * rhs.GetDenominator(), lhs.GetDenominator() * rhs.GetNumerator());
}
