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

#else

#include "daisysp.h"
#include "daisy_pod.h"

#define MAX_SIZE (48000 * 60 * 5) // 5 minutes of floats at 48 khz

using namespace daisysp;
using namespace daisy;

DaisyPod pod;

bool firstLoop = true;  //first loop (sets length)
bool rec   = false;     //currently recording
bool play  = false;     //currently playing

int                 pos = 0;
float DSY_SDRAM_BSS buf[MAX_SIZE];
int                 mod    = MAX_SIZE;
int                 len    = 0;
float               drywet = 0;
float               drywetBuf = 0;
bool                res    = false;

void ResetState()
{
    play  = false;
    rec   = false;
    firstLoop = true;
    pos   = 0;
    len   = 0;

    for(int i = 0; i < mod; i++)
        buf[i] = 0;

    mod = MAX_SIZE;
}

void Controls();

void NextSamples(float& output, AudioHandle::InterleavingInputBuffer in, size_t i);

void AudioCallback(AudioHandle::InterleavingInputBuffer  in, AudioHandle::InterleavingOutputBuffer out, size_t size)
{
    float output = 0;

    Controls();

    for(size_t i = 0; i < size; i += 2)
    {
        NextSamples(output, in, i);

        // left and right outs
        out[i] = out[i + 1] = output;
    }
}

void UpdateButtons()
{
    //button2 pressed
    if (pod.button2.RisingEdge())
    {
        if (firstLoop && rec)   //we were recording the first loop and now it stopped
        {
            //so set the loop length
            firstLoop = false;
            mod   = len;
            len   = 0;
        }

        res  = true;
        play = true;
        rec  = ! rec;   //first time button 2 is pressed, rec is false, so this makes it true
    }

    //button2 held
    if(pod.button2.TimeHeldMs() >= 1000 && res)
    {
        ResetState();
        res = false;
    }

    //button1 pressed and not empty buffer
    if (pod.button1.RisingEdge() && !(!rec && firstLoop))
    {
        play = ! play;
        rec  = false;
    }
}

//Deals with analog controls
void Controls()
{
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    drywet = pod.knob1.Process();

    UpdateButtons();

    //leds
    pod.led1.Set(0, play == true, 0);
    pod.led2.Set(rec == true, 0, 0);

    pod.UpdateLeds();
}

void WriteBuffer(AudioHandle::InterleavingInputBuffer in, size_t i)
{
    buf[pos] = buf[pos] * 0.5 + in[i] * 0.5;

    if (firstLoop)
        len++;
}

void NextSamples (float& output, AudioHandle::InterleavingInputBuffer in, size_t i)
{
    if (rec)
        WriteBuffer(in, i);

    output = buf[pos];

    //automatic looptime
    if(len >= MAX_SIZE)
    {
        firstLoop = false;
        mod   = MAX_SIZE;
        len   = 0;
    }

    if (play)
    {
        pos++;
        pos %= mod;
    }

    if (! rec)
        output = output * drywet + in[i] * (1 - drywet);
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
    pod.seed.StartLog ();

    // Set the number of samples processed per channel by the audio callback. Isn't 4 ridiculously low?
    pod.SetAudioBlockSize(4);

    ResetState();
    bool led_state { true };

    // start callback
    pod.StartAdc();
    pod.StartAudio(AudioCallback);

    while(1)
    {
        // blink the led
        pod.seed.SetLed(led_state);
        led_state = !led_state;

        PrintFloat ("dry wet", drywet, 3);

        System::Delay(1000);
    }
}



// /* ========================================= BLINK ========================================= */
// #include "daisy_seed.h"

// // Use the daisy namespace to prevent having to type
// // daisy:: before all libdaisy functions
// using namespace daisy;

// // Declare a DaisySeed object called hardware
// DaisySeed hardware;

// int main(void)
// {
//     // Declare a variable to store the state we want to set for the LED.
//     bool led_state;
//     led_state = true;

//     // Configure and Initialize the Daisy Seed
//     // These are separate to allow reconfiguration of any of the internal
//     // components before initialization.
//     hardware.Configure();
//     hardware.Init();

//     // Loop forever
//     for(;;)
//     {
//         // Set the onboard LED
//         hardware.SetLed(led_state);

//         // Toggle the LED state for the next time around.
//         led_state = !led_state;

//         // Wait 500ms
//         System::Delay(100);
//     }
// }
#endif