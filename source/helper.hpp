#pragma once

#include "daisy_pod.h"

inline void PrintFloat (daisy::DaisySeed& seed, const char *text, float value, int decimalPlaces)
{
    const auto wholeValue{static_cast<int> (value)};
    const auto fractionalValue{static_cast<int> (static_cast<float> (std::pow (10, decimalPlaces)) * (value - static_cast<float> (wholeValue)))};
    seed.PrintLine ("%s: %d.%d", text, wholeValue, fractionalValue);
}
