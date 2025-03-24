#include "fractional_number.hpp"

#include <imgui.h>
// For InputText with std::string
#include <misc/cpp/imgui_stdlib.h>

#include <iomanip>
#include <numeric>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <unordered_map>

FractionalNumber::FractionalNumber(const long long int n, const long long int d) : numerator(n), denominator(d)
{
    Simplify();
}

FractionalNumber::FractionalNumber(const std::string& s)
{
    static const std::unordered_map<char, int> precedence = {
        { '+', 1 },
        { '-', 1 },
        { '*', 2 },
        { '/', 2 },
    };

    std::vector<std::string> postfix;
    std::stack<char> operators;

    // Basically an implementation of the Shunting yard algorithm straight from the pseudocode at https://wikipedia.org/wiki/Shunting_yard_algorithm
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];

        // Skip spaces
        if (std::isspace(c))
        {
            continue;
        }

        // if it's a number
        if (std::isdigit(c))
        {
            const size_t start = i;
            while (i < s.size() && (std::isdigit(s[i]) || s[i] == '.'))
            {
                i += 1;
            }
            // Go back to the last digit
            i -= 1;
            // put it into the output queue
            postfix.push_back(s.substr(start, i - start + 1));
        }
        // if it's an operator o1
        else if (c == '+' || c == '-' || c == '*' || c == '/')
        {
            // while there is an operator o2 at the top of the operator stack which is not a left parenthesis,
            // and o2 has greater precedence than o1 (or the same precedence as we only deal with right associative operators)
            while (!operators.empty() && operators.top() != '(' && precedence.at(operators.top()) >= precedence.at(c))
            {
                // pop o2 from the operator stack into the output queue
                postfix.push_back(std::string(1, operators.top()));
                operators.pop();
            }
            // push o1 onto the operator stack
            operators.push(c);
        }
        // if it's a left parenthesis
        else if (c == '(')
        {
            // push it onto the operator stack
            operators.push(c);
        }
        // if it's a right parenthesis
        else if (c == ')')
        {
            // while the operator at the top of the operator stack is not a left parenthesis
            // If the stack runs out without finding a left parenthesis, then there are mismatched parentheses.
            while (!operators.empty() && operators.top() != '(')
            {
                // pop the operator from the operator stack into the output queue
                postfix.push_back(std::string(1, operators.top()));
                operators.pop();
            }
            // assert there is a left parenthesis at the top of the operator stack
            if (operators.empty() || operators.top() != '(')
            {
                throw std::invalid_argument("Mismatched parentheses");
            }
            // pop the left parenthesis from the operator stack and discard it
            operators.pop();
        }
    }

    // After the while loop, pop the remaining items from the operator stack into the output queue
    // while there are tokens on the operator stack
    while (!operators.empty())
    {
        // assert the operator on top of the stack is not a (left) parenthesis
        if (operators.top() == '(')
        {
            throw std::invalid_argument("Mismatched parentheses");
        }
        // pop the operator from the operator stack onto the output queue
        postfix.push_back(std::string(1, operators.top()));
        operators.pop();
    }

    std::stack<FractionalNumber> values;

    // Loop through the postfix tokens
    for (const auto& token : postfix)
    {
        // If it's a value, convert it to a fractional number
        if (!token.empty() && (std::isdigit(token[0]) || token[0] == '.'))
        {
            long long int numerator = 0;
            long long int denominator = 0;
            const size_t decimal_index = token.find('.');
            if (decimal_index == std::string::npos)
            {
                numerator = std::stoll(token);
                denominator = 1;
            }
            else
            {
                if (decimal_index == 0)
                {
                    numerator = 0;
                }
                else
                {
                    numerator = std::stoll(token.substr(0, decimal_index));
                }
                const long long int decimal = std::stoll(token.substr(decimal_index + 1));
                denominator = 1;
                for (size_t i = 0; i < token.size() - decimal_index - 1; ++i)
                {
                    numerator *= 10;
                    denominator *= 10;
                }
                numerator += decimal;
            }
            values.push(FractionalNumber(numerator, denominator));
        }
        // If it's an operator, apply it
        else
        {
            if (values.size() < 2)
            {
                throw std::invalid_argument("Invalid expression");
            }
            const FractionalNumber b = values.top();
            values.pop();
            const FractionalNumber a = values.top();
            values.pop();
            switch (token[0])
            {
            case '+':
                values.push(a + b);
                break;
            case '-':
                values.push(a - b);
                break;
            case '*':
                values.push(a * b);
                break;
            case '/':
                if (b.GetNumerator() == 0)
                {
                    throw std::invalid_argument("Division by zero");
                }
                values.push(a / b);
                break;
            default:
                throw std::invalid_argument("Invalid operator");
                break;
            }
        }
    }

    if (values.size() != 1)
    {
        throw std::invalid_argument("Invalid expression");
    }

    numerator = values.top().GetNumerator();
    denominator = values.top().GetDenominator();
    // Fraction is already simplified, we just need to update value
    UpdateValue();
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

void FractionalNumber::RenderInputText(const char* label, const bool disabled, const bool fraction_tooltip, float width)
{
    std::string& float_value = GetStringFloat();
    if (width == 0.0f)
    {
        width = ImGui::CalcTextSize(float_value.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    }

    ImGui::BeginDisabled(disabled);
    ImGui::SetNextItemWidth(width);
    ImGui::InputText(label, &float_value, disabled ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);
    ImGui::EndDisabled();
    if (fraction_tooltip)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", GetStringFraction().c_str());
        }
    }
}

FractionalNumber& FractionalNumber::operator*=(const FractionalNumber& rhs)
{
    numerator *= rhs.numerator;
    denominator *= rhs.denominator;
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
    const long long int gcd = numerator == 0 ? denominator : (std::gcd(numerator, denominator) * (denominator < 0 ? -1LL : 1LL));
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
