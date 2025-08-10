#include "effects.hpp"

daisysp::ReverbSc DSY_SDRAM_BSS                       reverbSC;
daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS leftDelay;
daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS rightDelay;