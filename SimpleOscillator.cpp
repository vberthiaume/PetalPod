#if 0
/* ========================================= SIMPLE OSCILLATOR ========================================= */

#include "daisy_pod.h"
#include "daisysp.h"

#define NUM_WAVEFORMS 4

//TODO: remove these and move this whole folder up into the top of the repo, or something that actually makes sense
using namespace daisy;
using namespace daisysp;

DaisyPod   hw;
Oscillator osc;
Parameter  frequencyParameter;
Parameter  volumeParameter;

uint8_t waveforms[NUM_WAVEFORMS] =
{
    Oscillator::WAVE_SIN,
    Oscillator::WAVE_TRI,
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
};

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    hw.ProcessDigitalControls();

    static int waveform {0};
    waveform += hw.encoder.Increment();
    waveform = DSY_CLAMP(waveform, 0, NUM_WAVEFORMS);
    osc.SetWaveform(waveforms[waveform]);

    static int octave {0};
    if (hw.button2.RisingEdge())
        ++octave;
    if (hw.button1.RisingEdge())
        --octave;
    octave = DSY_CLAMP(octave, 0, 4);

    // convert MIDI to frequency and multiply by octave size
    const auto freq = mtof (frequencyParameter.Process() + (octave * 12));
    osc.SetFreq (freq);

    osc.SetAmp (volumeParameter.Process());

    // Audio Loop
    for (size_t i = 0; i < size; i += 2)
    {
        // Process
        const auto sig = osc.Process();
        out[i] = sig;
        out[i + 1] = sig;
    }
}

int main(void)
{
    // Init everything
    hw.Init();
    hw.SetAudioBlockSize(4);

    // Init freq Parameter to knob1 using MIDI note numbers min 0, max 127, curve linear
    frequencyParameter.Init (hw.knob1, 0, 127, Parameter::LINEAR);
    volumeParameter.Init (hw.knob2, 0.f, 1.f, Parameter::LINEAR);

    osc.Init(hw.AudioSampleRate());
    // osc.SetAmp (1.f);

    // start callbacks
    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1) {}
}

#elif 0

#include "daisysp.h"
#include "daisy_pod.h"

// Set max delay time to 0.75 of samplerate.
#define MAX_DELAY static_cast<size_t>(48000 * 2.5f)
#define REV 0
#define DEL 1
#define CRU 2


using namespace daisysp;
using namespace daisy;

static DaisyPod pod;

static ReverbSc                                  rev;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;
static Tone                                      tone;
static Parameter deltime, cutoffParam, crushrate;
int              mode = REV;

float currentDelay, feedback, delayTarget, cutoff;

int   crushmod, crushcount;
float crushsl, crushsr, drywet;


void UpdateEffectKnobs(float &k1, float &k2)
{
    k1 = pod.knob1.Process();
    k2 = pod.knob2.Process();

    switch(mode)
    {
        case REV:
            drywet = k1;
            rev.SetFeedback(k2);
            break;
        case DEL:
            delayTarget = deltime.Process();
            feedback    = k2;
            break;
        case CRU:
            cutoff = cutoffParam.Process();
            tone.SetFreq(cutoff);
            crushmod = (int)crushrate.Process();
    }
}

void UpdateEncoder()
{
    mode = mode + pod.encoder.Increment();
    mode = (mode % 3 + 3) % 3;
}

void UpdateLeds(float k1, float k2)
{
    pod.led1.Set(k1 * (mode == 2), k1 * (mode == 1), k1 * (mode == 0 || mode == 2));
    pod.led2.Set(k2 * (mode == 2), k2 * (mode == 1), k2 * (mode == 0 || mode == 2));

    pod.UpdateLeds();
}

void Controls()
{
    float k1, k2;
    delayTarget = feedback = drywet = 0;

    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    UpdateEffectKnobs(k1, k2);

    UpdateEncoder();

    UpdateLeds(k1, k2);
}

void GetReverbSample(float &outl, float &outr, float inl, float inr)
{
    rev.Process(inl, inr, &outl, &outr);
    outl = drywet * outl + (1 - drywet) * inl;
    outr = drywet * outr + (1 - drywet) * inr;
}

void GetDelaySample(float &outl, float &outr, float inl, float inr)
{
    fonepole(currentDelay, delayTarget, .00007f);
    delr.SetDelay(currentDelay);
    dell.SetDelay(currentDelay);
    outl = dell.Read();
    outr = delr.Read();

    dell.Write((feedback * outl) + inl);
    outl = (feedback * outl) + ((1.0f - feedback) * inl);

    delr.Write((feedback * outr) + inr);
    outr = (feedback * outr) + ((1.0f - feedback) * inr);
}

void GetCrushSample(float &outl, float &outr, float inl, float inr)
{
    crushcount++;
    crushcount %= crushmod;
    if(crushcount == 0)
    {
        crushsr = inr;
        crushsl = inl;
    }
    outl = tone.Process(crushsl);
    outr = tone.Process(crushsr);
}


void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    float outl, outr, inl, inr;

    Controls();

    //audio
    for(size_t i = 0; i < size; i += 2)
    {
        inl = in[i];
        inr = in[i + 1];

        switch(mode)
        {
            case REV: GetReverbSample(outl, outr, inl, inr); break;
            case DEL: GetDelaySample(outl, outr, inl, inr); break;
            case CRU: GetCrushSample(outl, outr, inl, inr); break;
            default: outl = outr = 0;
        }

        // left out
        out[i] = outl;

        // right out
        out[i + 1] = outr;
    }
}

int main(void)
{
    // initialize pod hardware and oscillator daisysp module
    float sample_rate;

    //Inits and sample rate
    pod.Init();
    pod.SetAudioBlockSize(4);
    sample_rate = pod.AudioSampleRate();
    rev.Init(sample_rate);
    dell.Init();
    delr.Init();
    tone.Init(sample_rate);

    //set parameters
    deltime.Init(pod.knob1, sample_rate * .05, MAX_DELAY, deltime.LOGARITHMIC);
    cutoffParam.Init(pod.knob1, 500, 20000, cutoffParam.LOGARITHMIC);
    crushrate.Init(pod.knob2, 1, 50, crushrate.LOGARITHMIC);

    //reverb parameters
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);

    //delay parameters
    currentDelay = delayTarget = sample_rate * 0.75f;
    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    // start callback
    pod.StartAdc();
    pod.StartAudio(AudioCallback);

    while(1) {}
}

#else

#include "daisysp.h"
#include "daisy_pod.h"

#define MAX_SIZE (48000 * 60 * 5)                   // 5 minutes of floats at 48 khz
#define MAX_DELAY static_cast<size_t>(48000 * 2.5f) // Set max delay time to 0.75 of samplerate.
#define REV 0
#define DEL 1
#define CRU 2

using namespace daisysp;
using namespace daisy;

DaisyPod pod;

//looper things
bool isFirstLoop          = true;   //the first loop will set the length for the buffer
bool isCurrentlyRecording = false;
bool isCurrentlyPlaying   = false;

int                 pos = 0;
float DSY_SDRAM_BSS buf[MAX_SIZE];  //DSY_SDRAM_BSS means this buffer will live in SDRAM, see https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
int                 mod       = MAX_SIZE;
int                 len       = 0;

float               drywet    = 0;
float               drywetBuf = 0;
bool                res       = false;
bool                led_state = true;

//effect things
static ReverbSc                                  rev;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;
static Tone                                      tone;
static daisy::Parameter                          deltime, cutoffParam, crushrate;
int                                              mode = REV;

float currentDelay, feedback, delayTarget, cutoff;

int   crushmod, crushcount;
float crushsl, crushsr/*, drywet*/;

void ResetLooperState()
{
    isCurrentlyPlaying   = false;
    isCurrentlyRecording = false;
    isFirstLoop          = true;
    pos                  = 0;
    len                  = 0;

    for(int i = 0; i < mod; i++)
        buf[i] = 0;

    mod = MAX_SIZE;
}

void GetReverbSample(float &outl, float &outr, float inl, float inr)
{
    rev.Process(inl, inr, &outl, &outr);
    outl = drywet * outl + (1 - drywet) * inl;
    outr = drywet * outr + (1 - drywet) * inr;
}

void GetDelaySample(float &outl, float &outr, float inl, float inr)
{
    fonepole(currentDelay, delayTarget, .00007f);
    delr.SetDelay(currentDelay);
    dell.SetDelay(currentDelay);
    outl = dell.Read();
    outr = delr.Read();

    dell.Write((feedback * outl) + inl);
    outl = (feedback * outl) + ((1.0f - feedback) * inl);

    delr.Write((feedback * outr) + inr);
    outr = (feedback * outr) + ((1.0f - feedback) * inr);
}

void GetCrushSample(float &outl, float &outr, float inl, float inr)
{
    crushcount++;
    crushcount %= crushmod;
    if(crushcount == 0)
    {
        crushsr = inr;
        crushsl = inl;
    }
    outl = tone.Process(crushsl);
    outr = tone.Process(crushsr);
}

void UpdateButtons()
{
    //button2 pressed
    if (pod.button2.RisingEdge())
    {
        if (isFirstLoop && isCurrentlyRecording)   //we were recording the first loop and now it stopped
        {
            //so set the loop length
            isFirstLoop = false;
            mod = len;
            len = 0;
        }

        res                  = true;
        isCurrentlyPlaying   = true;
        isCurrentlyRecording = ! isCurrentlyRecording;
    }

    //button2 held
    if (pod.button2.TimeHeldMs() >= 1000 && res)
    {
        ResetLooperState();
        res = false;
    }

    //button1 pressed and not empty buffer
    if (pod.button1.RisingEdge() && (isCurrentlyRecording || ! isFirstLoop))
    {
        isCurrentlyPlaying   = ! isCurrentlyPlaying;
        isCurrentlyRecording = false;
    }
}

void UpdateEffectKnobs(float &k1, float &k2)
{
    k1 = pod.knob1.Process();
    k2 = pod.knob2.Process();

    switch(mode)
    {
        case REV:
            drywet = k1;
            rev.SetFeedback(k2);
            break;
        case DEL:
            delayTarget = deltime.Process();
            feedback    = k2;
            break;
        case CRU:
            cutoff = cutoffParam.Process();
            tone.SetFreq(cutoff);
            crushmod = (int)crushrate.Process();
    }
}

void UpdateEncoder()
{
    mode = mode + pod.encoder.Increment();
    mode = (mode % 3 + 3) % 3;
}

void ProcessControls()
{
    float k1, k2;
    delayTarget = feedback = drywet = 0;

    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    drywet = pod.knob1.Process();

    UpdateButtons();

    UpdateEffectKnobs(k1, k2);

    UpdateEncoder();

    //leds
    pod.led1.Set (0, isCurrentlyPlaying == true, 0);
    pod.led2.Set (isCurrentlyRecording == true, 0, 0);

    pod.UpdateLeds();
}

void RecordIntoBuffer(AudioHandle::InterleavingInputBuffer in, size_t i)
{
    buf[pos] = buf[pos] * 0.5 + in[i] * 0.5;

    if(isFirstLoop)
        len++;
}

float NextSamples (AudioHandle::InterleavingInputBuffer in, size_t i)
{
    if (isCurrentlyRecording)
        RecordIntoBuffer(in, i);

    float output = buf[pos];

    //automatic looptime
    if(len >= MAX_SIZE)
    {
        isFirstLoop = false;
        mod   = MAX_SIZE;
        len   = 0;
    }

    if (isCurrentlyPlaying)
    {
        pos++;
        pos %= mod;
    }

    if (! isCurrentlyRecording)
        output = output * drywet + in[i] * (.968f - drywet);    //slider apparently only goes to .968f lol

    return output;
}

void AudioCallback (AudioHandle::InterleavingInputBuffer inputBuffer,
                    AudioHandle::InterleavingOutputBuffer outputBuffer,
                    size_t numSamples)
{
    float outl, outr, inl, inr;

    ProcessControls();

    for(size_t curSample = 0; curSample < numSamples; curSample += 2)
    {
        float output = NextSamples (inputBuffer, curSample);

        // left and right outs
        outputBuffer[curSample] = outputBuffer[curSample + 1] = output;

        //applying effects right on the output buffer
        inl = outputBuffer[curSample];
        inr = outputBuffer[curSample + 1];

        switch(mode)
        {
            case REV: GetReverbSample (outl, outr, inl, inr); break;
            case DEL: GetDelaySample (outl, outr, inl, inr); break;
            case CRU: GetCrushSample (outl, outr, inl, inr); break;
            default: outl = outr = 0;
        }

        outputBuffer[curSample] = outl;
        outputBuffer[curSample + 1] = outr;
    }
}

void PrintFloat (const char* text, float value, int decimalPlaces)
{
    const auto wholeValue { static_cast<int> (value) };
    const auto fractionalValue { static_cast<int> (static_cast<float> (std::pow (10, decimalPlaces)) * (value - static_cast<float> (wholeValue))) };
    pod.seed.PrintLine ("%s: %d.%d", text, wholeValue, fractionalValue);
}

int main(void)
{
    // initialize pod hardware and logger
    pod.Init();
    pod.SetAudioBlockSize(4);   // Set the number of samples processed per channel by the audio callback. Isn't 4 ridiculously low?
    pod.seed.StartLog ();

    ResetLooperState();

    //init everything related to effects
    float sample_rate = pod.AudioSampleRate();
    rev.Init(sample_rate);
    dell.Init();
    delr.Init();
    tone.Init(sample_rate);

    //set parameters
    deltime.Init(pod.knob1, sample_rate * .05, MAX_DELAY, deltime.LOGARITHMIC);
    cutoffParam.Init(pod.knob1, 500, 20000, cutoffParam.LOGARITHMIC);
    crushrate.Init(pod.knob2, 1, 50, crushrate.LOGARITHMIC);

    //reverb parameters
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);

    //delay parameters
    currentDelay = delayTarget = sample_rate * 0.75f;
    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    // start audio
    pod.StartAdc();
    pod.StartAudio (AudioCallback);

    while(1)
    {
        // blink the led
        pod.seed.SetLed(led_state);
        led_state = !led_state;

        PrintFloat ("dry wet", drywet, 3);

        System::Delay(1000);
    }
}

#endif
