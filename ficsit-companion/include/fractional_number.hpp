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

    /// @brief Render the float value in an input text
    /// @param label ImGui label of the InputText element
    /// @param disabled If true, the element will not be interactable
    /// @param fraction_tooltip If true, will add an ImGui tooltip with the fraction value
    /// @param fixed_width If != 0.0f, will set the width to this value, if 0.0f, the width with adjust to the content
    void RenderInputText(const char* label, const bool disabled, const bool fraction_tooltip, float fixed_width = 0.0f);

    FractionalNumber& operator*=(const FractionalNumber& rhs);
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
