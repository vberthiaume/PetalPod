#include "daisysp.h"
#include "helper.hpp"

daisy::DaisyPod pod;

//looper things
constexpr auto maxRecordingSize     = 48000 * 60 * 5; // 5 minutes of floats at 48 khz
constexpr auto fadeOutLength        = 30;             // the number of samples at the end of the loop where we apply a linear fade out
bool           isFirstLoop          = true;           // the first loop will set the length for the buffer
bool           isCurrentlyRecording = false;
bool           isCurrentlyPlaying   = false;

float DSY_SDRAM_BSS looperBuffer[maxRecordingSize]; //DSY_SDRAM_BSS means this buffer will live in SDRAM, see https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
int                 positionInLooperBuffer = 0;
int                 cappedRecordingSize = maxRecordingSize;
int                 numRecordedSamples = 0;

//effect things
constexpr size_t maxDelayTime{static_cast<size_t> (48000 * 2.5f)}; // Set max delay time to 0.75 of samplerate.

enum fxMode
{
    reverb = 0,
    delay,
    crush,
    total
};

daisysp::ReverbSc                                     reverbSC;
daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS leftDelay;
daisysp::DelayLine<float, maxDelayTime> DSY_SDRAM_BSS rightDelay;
daisysp::Tone                                         tone;
daisy::Parameter                                      delayTime, cutoffParam, crushrate;
int                                                   curFxMode = fxMode::reverb;

float currentDelay, feedback, delayTarget, cutoff;
int   crushmod, crushcount;
float crushsl, crushsr, drywet;

void ResetLooperState()
{
    isCurrentlyPlaying   = false;
    isCurrentlyRecording = false;
    isFirstLoop          = true;
    positionInLooperBuffer                  = 0;
    numRecordedSamples                  = 0;

    for (int i = 0; i < cappedRecordingSize; i++)
        looperBuffer[i] = 0;

    cappedRecordingSize = maxRecordingSize;
}

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

#if 1
void UpdateButtons()
{
    //button 1 just started to be held button: start recording
    if (pod.button1.RisingEdge())
    {
        isCurrentlyPlaying   = true;
        isCurrentlyRecording = true;
    }

    //button 1 was just released: stop recording
    if (pod.button1.FallingEdge())
    {
        if (isFirstLoop && isCurrentlyRecording) //we were recording the first loop and now it stopped
        {
            //so set the loop length
            isFirstLoop         = false;
            cappedRecordingSize = numRecordedSamples;
            numRecordedSamples  = 0;

            const auto diff{cappedRecordingSize - fadeOutLength};
            const auto actualFadeOut = diff > 0 ? diff : cappedRecordingSize;
            for (int i = cappedRecordingSize; i > cappedRecordingSize - actualFadeOut; --i)
            {
                //NOW HERE DEBUG THIS
                const auto mult{(1 - i) / actualFadeOut};
                looperBuffer[i] *= mult;
            }
        }

        isCurrentlyRecording = false;
    }
}
#else
void UpdateButtons()
{
    //button2 pressed
    if (pod.button2.RisingEdge())
    {
        if (isFirstLoop && isCurrentlyRecording) //we were recording the first loop and now it stopped
        {
            //so set the loop length
            isFirstLoop = false;
            mod         = len;
            len         = 0;
        }

        res                  = true;
        isCurrentlyPlaying   = true;
        isCurrentlyRecording = !isCurrentlyRecording;
    }

    //button2 held
    if (pod.button2.TimeHeldMs() >= 1000 && res)
    {
        ResetLooperState();
        res = false;
    }

    //button1 pressed and not empty buffer
    if (pod.button1.RisingEdge() && (isCurrentlyRecording || !isFirstLoop))
    {
        isCurrentlyPlaying   = !isCurrentlyPlaying;
        isCurrentlyRecording = false;
    }
}
#endif

void UpdateEffectKnobs (float &k1, float &k2)
{
    drywet = pod.knob1.Process();
    k1 = drywet;
    k2 = pod.knob2.Process();

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

void UpdateEncoder()
{
    curFxMode = curFxMode + pod.encoder.Increment();
    curFxMode = (curFxMode % fxMode::total + fxMode::total) % fxMode::total;
}

void UpdateLeds(float k1, float k2)
{
    //led1 is red when recording, green when playing, off otherwise
    daisy::Color led1Color;
    if (isCurrentlyRecording)
        led1Color.Init (daisy::Color::RED);
    else if (isCurrentlyPlaying)
        led1Color.Init (daisy::Color::GREEN);
    pod.led1.SetColor (led1Color);

    //led 2 reflects the effect parameter
    pod.led2.Set (k2 * (curFxMode == 2), k2 * (curFxMode == 1), k2 * (curFxMode == 0 || curFxMode == 2));

    pod.UpdateLeds();
}

void ProcessControls()
{
    float k1, k2;
    delayTarget = 0;
    feedback = 0;
    drywet = 0;

    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    UpdateButtons();

    UpdateEffectKnobs (k1, k2);

    UpdateEncoder();

    UpdateLeds (k1, k2);
}

float GetLooperSample (daisy::AudioHandle::InterleavingInputBuffer in, size_t i)
{
    //this should basically only happen when we start and we don't have anything recorded
    if (! isCurrentlyRecording && ! isCurrentlyPlaying)
        return in[i];

    if (isCurrentlyRecording)
    {
        //old, overdub way of recording. How weird to have the current one at .5. Probably a cheap way to avoid clipping
        //looperBuffer[positionInLooperBuffer] = looperBuffer[positionInLooperBuffer] * 0.5 + in[i] * 0.5;
        looperBuffer[positionInLooperBuffer] = in[i];

        if (isFirstLoop)
            ++numRecordedSamples;
    }

    const auto outputSample = looperBuffer[positionInLooperBuffer];

    //truncate loop because we went over our max recording size
    if (numRecordedSamples >= maxRecordingSize)
    {
        isFirstLoop         = false;
        cappedRecordingSize = maxRecordingSize;
        numRecordedSamples  = 0;
    }

    if (isCurrentlyPlaying)
    {
        ++positionInLooperBuffer;
        positionInLooperBuffer %= cappedRecordingSize;
    }

    //this was to use knob 1 as a dry/wet for the in/out for the looper. Keeping in case it's useful later
    // if (! isCurrentlyRecording)
    //     outputSample = outputSample * drywet + in[i] * (.968f - drywet); //slider apparently only goes to .968f lol

    return outputSample;
}

void AudioCallback (daisy::AudioHandle::InterleavingInputBuffer  inputBuffer,
                    daisy::AudioHandle::InterleavingOutputBuffer outputBuffer,
                    size_t                                       numSamples)
{
    ProcessControls();

    float outputLeft, outputRight, inputLeft, inputRight;
    for (size_t curSample = 0; curSample < numSamples; curSample += 2)
    {
        //get looper output
        const auto looperOutput { GetLooperSample (inputBuffer, curSample) };
        outputBuffer[curSample] = outputBuffer[curSample + 1] = looperOutput;

        //apply effects
        inputLeft = outputBuffer[curSample];
        inputRight = outputBuffer[curSample + 1];

        switch (curFxMode)
        {
            case fxMode::reverb: GetReverbSample (outputLeft, outputRight, inputLeft, inputRight); break;
            case fxMode::delay: GetDelaySample (outputLeft, outputRight, inputLeft, inputRight); break;
            case fxMode::crush: GetCrushSample (outputLeft, outputRight, inputLeft, inputRight); break;
            default: outputLeft = outputRight = 0;
        }

        outputBuffer[curSample]     = outputLeft;
        outputBuffer[curSample + 1] = outputRight;
    }
}

int main (void)
{
    // initialize pod hardware and logger
    pod.Init();
    pod.SetAudioBlockSize (4); // Set the number of samples processed per channel by the audio callback. Isn't 4 ridiculously low?
    pod.seed.StartLog();

    ResetLooperState();

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

    // start audio
    pod.StartAdc();
    pod.StartAudio (AudioCallback);

    bool led_state = true;
    while (1)
    {
        // blink the led
        pod.seed.SetLed (led_state);
        led_state = !led_state;

        PrintFloat (pod.seed, "dry wet", drywet, 3);

        daisy::System::Delay (1000);
    }
}
