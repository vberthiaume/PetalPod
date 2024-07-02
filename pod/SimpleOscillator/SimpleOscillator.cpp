#include "daisy_pod.h"
#include "daisysp.h"

#define NUM_WAVEFORMS 4

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
