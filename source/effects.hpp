#pragma once

#include "daisysp.h"
#include "daisy_pod.h"

static constexpr auto maxDelayTime { static_cast<size_t> (48000 * 2.5f) }; // Set max delay time to 0.75 of samplerate.

enum fxMode
{
    reverb = 0,
    delay,
    crush,
    total
};

extern daisysp::ReverbSc DSY_SDRAM_BSS                       reverbSC;
extern daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS leftDelay;
extern daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS rightDelay;

struct Effects
{
    // Tone is a first-order recursive low-pass filter with variable frequency response, but it's used for crushing here
    daisysp::Tone    tone;
    daisy::Parameter delayTime, cutoffParam, crushrate;
    int              curFxMode = fxMode::delay;

    float currentDelay, feedback, delayTarget, cutoff;
    int   crushmod, crushcount;
    float crushsl, crushsr, drywet = 1.f;

    void GetReverbSample (float &outl, float &outr, float inl, float inr)
    {
        reverbSC.Process (inl, inr, &outl, &outr);
        outl = drywet * outl + (1 - drywet) * inl;
        outr = drywet * outr + (1 - drywet) * inr;
    }

    void GetDelaySample (float &outl, float &outr, float inl, float inr)
    {
        daisysp::fonepole (currentDelay, delayTarget, .00007f);
        rightDelay.SetDelay (currentDelay);
        leftDelay.SetDelay (currentDelay);
        outl = leftDelay.Read();
        outr = rightDelay.Read();

        leftDelay.Write ((feedback * outl) + inl);
        outl = (feedback * outl) + ((1.0f - feedback) * inl);

        rightDelay.Write ((feedback * outr) + inr);
        outr = (feedback * outr) + ((1.0f - feedback) * inr);
    }

    void GetCrushSample (float &outl, float &outr, float inl, float inr)
    {
        crushcount++;
        crushcount %= crushmod;
        if (crushcount == 0)
        {
            crushsr = inr;
            crushsl = inl;
        }
        outl = tone.Process (crushsl);
        outr = tone.Process (crushsr);
    }

    void UpdateEffectKnobs (daisy::DaisyPod &pod, float &k1, float &k2)
    {
        drywet = pod.knob1.Process();
        k1     = drywet;
        k2     = pod.knob2.Process();

        switch (curFxMode)
        {
            case fxMode::reverb:
                drywet = k1;
                reverbSC.SetFeedback (k2);
                break;
            case fxMode::delay:
                delayTarget = delayTime.Process();
                feedback    = k2;
                break;
            case fxMode::crush:
                cutoff = cutoffParam.Process();
                tone.SetFreq (cutoff);
                crushmod = (int) crushrate.Process();
        }
    }

    void UpdateEncoder (daisy::DaisyPod &pod)
    {
        //rotating the encoder changes the effects
        curFxMode = curFxMode + pod.encoder.Increment();
        curFxMode = (curFxMode % fxMode::total + fxMode::total) % fxMode::total;

        //TODO: pushing the encoder should do something
        // if (pod.encoder.RisingEdge())
    }

    void ProcessEffectsControls (daisy::DaisyPod &pod, float &k1, float &k2)
    {
        delayTarget = 0;
        feedback    = 0;
        drywet      = 0;

        UpdateEffectKnobs (pod, k1, k2);
        UpdateEncoder (pod);
    }

    void initEffects (daisy::DaisyPod &pod)
    {
        //init everything related to effects
        float sample_rate = pod.AudioSampleRate();
        reverbSC.Init (sample_rate);
        leftDelay.Init();
        rightDelay.Init();
        tone.Init (sample_rate);

        //init parameters
        delayTime.Init (pod.knob1, sample_rate * .05, maxDelayTime, delayTime.LOGARITHMIC);
        cutoffParam.Init (pod.knob1, 500, 20000, cutoffParam.LOGARITHMIC);
        crushrate.Init (pod.knob2, 1, 50, crushrate.LOGARITHMIC);

        //reverb parameters
        reverbSC.SetLpFreq (18000.0f);
        reverbSC.SetFeedback (0.85f);

        //delay parameters
        currentDelay = delayTarget = sample_rate * 0.75f;
        leftDelay.SetDelay (currentDelay);
        rightDelay.SetDelay (currentDelay);
    }
};
