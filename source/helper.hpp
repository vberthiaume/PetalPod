#pragma once

#include "daisy_pod.h"

inline void PrintFloat (daisy::DaisySeed& seed, const char *text, float value, int decimalPlaces)
{
    const auto wholeValue{static_cast<int> (value)};
    const auto fractionalValue{static_cast<int> (static_cast<float> (std::pow (10, decimalPlaces)) * (value - static_cast<float> (wholeValue)))};
    seed.PrintLine ("%s: %d.%d", text, wholeValue, fractionalValue);
}

/** Remaps a value from a source range to a target range. */
template <typename Type>
Type jmap (Type sourceValue, Type sourceRangeMin, Type sourceRangeMax, Type targetRangeMin, Type targetRangeMax)
{
    assert (abs (sourceRangeMax - sourceRangeMin) > 0); // mapping from a range of zero will produce NaN!
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) / (sourceRangeMax - sourceRangeMin);
}
