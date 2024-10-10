#pragma once

#include "daisy_pod.h"

//TODO: have the implementation in a cpp file and remove inline to see if that reduces code size, I'm sure it does
inline void PrintFloat (daisy::DaisySeed& seed, daisy::FixedCapStr<16> str, float value, int decimalPlaces)
{
    str.AppendFloat (value, decimalPlaces);
    seed.PrintLine (str);
}

inline void PrintFloat (daisy::DaisySeed& seed, float value)
{
    const auto wholeValue{static_cast<int> (value)};
    const auto decimalPlaces {3};
    const auto fractionalValue{static_cast<int> (static_cast<float> (std::pow (10, decimalPlaces)) * (value - static_cast<float> (wholeValue)))};
    seed.Print ("%d.%d ", wholeValue, fractionalValue);
}

/** Remaps a value from a source range to a target range. */
template <typename Type>
Type jmap (Type sourceValue, Type sourceRangeMin, Type sourceRangeMax, Type targetRangeMin, Type targetRangeMax)
{
    assert (abs (sourceRangeMax - sourceRangeMin) > 0); // mapping from a range of zero will produce NaN!
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) / (sourceRangeMax - sourceRangeMin);
}
