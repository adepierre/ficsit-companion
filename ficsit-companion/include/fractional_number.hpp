#pragma once

#include <string>
#include <optional>

class FractionalNumber
{
public:
    FractionalNumber(const long long int n = 0, const long long int d = 1);
    FractionalNumber(const std::string& s);

    long long int GetNumerator() const;
    long long int GetDenominator() const;
    double GetValue() const;

    std::string& GetStringFraction();
    std::string& GetStringFloat();

    FractionalNumber& operator*=(const FractionalNumber& rhs);
    FractionalNumber& operator/=(const FractionalNumber& rhs);
    FractionalNumber& operator+=(const FractionalNumber& rhs);
    FractionalNumber& operator-=(const FractionalNumber& rhs);
    bool operator!=(const FractionalNumber& other) const;
    bool operator==(const FractionalNumber& other) const;
    bool operator<(const FractionalNumber& other) const;
    bool operator>(const FractionalNumber& other) const;

private:
    void Simplify();
    void UpdateValue();

private:
    long long int numerator;
    long long int denominator;
    double value;
    std::optional<std::string> str_fraction;
    std::optional<std::string> str_float;
};

FractionalNumber operator*(FractionalNumber lhs, const FractionalNumber& rhs);
FractionalNumber operator+(FractionalNumber lhs, const FractionalNumber& rhs);
FractionalNumber operator-(FractionalNumber lhs, const FractionalNumber& rhs);
FractionalNumber operator/(const FractionalNumber& lhs, const FractionalNumber& rhs);
