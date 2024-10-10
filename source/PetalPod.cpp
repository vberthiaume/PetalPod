#include "daisysp.h"
#include "helper.hpp"
#include "fatfs.h"

#include <atomic>

daisy::DaisyPod pod;

#define WAIT_FOR_SERIAL_MONITOR 0
#define ENABLE_INPUT_DETECTION 1
#define ENABLE_ALL_EFFECTS 1

//looper things
constexpr auto maxRecordingSize     = 48000 * 60 * 5; // 5 minute of floats at 48 khz.
constexpr auto fadeOutLength        = 1000;           // the number of samples at the end of the loop where we apply a linear fade out
bool           isFirstLoop          = true;           // the first loop will set the length for the buffer
bool           isCurrentlyRecording = false;
bool           isCurrentlyPlaying   = false;
bool           couldBeReset         = false;

//DSY_SDRAM_BSS means this buffer will live in SDRAM, see https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
float DSY_SDRAM_BSS looperBuffer[maxRecordingSize];
int                 positionInLooperBuffer = 0;
int                 cappedRecordingSize    = maxRecordingSize;
int                 numRecordedSamples     = 0;

//effect things
#if ENABLE_ALL_EFFECTS
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
#endif
float crushsl, crushsr, drywet = 1.f;

#if ENABLE_INPUT_DETECTION
//input detection
bool           isWaitingForInput    = false;
bool           gotPreviousSample       = false;
float          previousSample          = -1000.f;
constexpr auto inputDetectionThreshold = .025f;
#endif

//file saving
std::atomic<bool>     needToSave { false };
std::atomic<bool>     needToDelete { false };
daisy::SdmmcHandler   sdmmc;
daisy::FatFSInterface fsi;
constexpr const char *loopFileName { "loop.wav" };
FIL                   loopFile;

void ResetLooperState()
{
    isFirstLoop          = true; // the first loop will set the length for the buffer
    isCurrentlyRecording = false;
    isCurrentlyPlaying   = false;

#if ENABLE_INPUT_DETECTION
    isWaitingForInput    = false;
    gotPreviousSample    = false;
#endif
    positionInLooperBuffer = 0;
    numRecordedSamples     = 0;

    for (int i = 0; i < cappedRecordingSize; i++)
        looperBuffer[i] = 0;

    cappedRecordingSize = maxRecordingSize;
}

#if ENABLE_ALL_EFFECTS
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
#endif

void FadeOutLooperBuffer()
{
    const auto diff          = cappedRecordingSize - fadeOutLength;
    const auto actualFadeOut = (diff > fadeOutLength) ? fadeOutLength : cappedRecordingSize;
    for (int i = cappedRecordingSize; i > cappedRecordingSize - actualFadeOut; --i)
    {
        //map i values to a ramp that goes from 0 to 1
        const auto ramp = jmap (static_cast<float> (i), static_cast<float> (cappedRecordingSize),
                                static_cast<float> (cappedRecordingSize - actualFadeOut), 0.f, 1.f);

        //ramp out looper buffer
        looperBuffer[i] *= ramp;
    }
}

void StopRecording()
{
    //stop recording, set the loop length and fade out buffer
    couldBeReset         = true;
    isCurrentlyRecording = false;
    isFirstLoop          = false;
    cappedRecordingSize  = numRecordedSamples;
    numRecordedSamples   = 0;
}

void UpdateButtons()
{
    //button 1 pressed
    if (pod.button1.RisingEdge())
    {
        if (isFirstLoop)
        {
            //button 1 pressed for the first time, we wait for input
            if (! isCurrentlyRecording)
            {
#if ENABLE_INPUT_DETECTION
                isWaitingForInput = true;
#else
                isCurrentlyPlaying   = true;
                isCurrentlyRecording = true;
#endif
            }
            //button 1 pressed for the second time
            else
            {
#if ENABLE_INPUT_DETECTION
                //we never detected any input so we didn't record anything
                if (isWaitingForInput)
                {
                    isWaitingForInput = false;
                }
                //we did record something
                else
#endif
                {
                    StopRecording();
                    FadeOutLooperBuffer();
                    needToSave.store (true);    //trigger a save in the main loop, you can't do file operations in the audio thread
                }
            }
        }
        //ok so for now after the first loop we don't record
        else
        {
            isCurrentlyPlaying = ! isCurrentlyPlaying;
        }
    }

    //button 1 held
    if (pod.button1.TimeHeldMs() >= 1000 && couldBeReset)
    {
        ResetLooperState();
        needToDelete.store (true);
        couldBeReset = false;
    }
}

#if ENABLE_ALL_EFFECTS
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
    //rotating the encoder changes the effects
    curFxMode = curFxMode + pod.encoder.Increment();
    curFxMode = (curFxMode % fxMode::total + fxMode::total) % fxMode::total;

    //TODO: pushing the encoder should do something
    // if (pod.encoder.RisingEdge())
}
#endif
void UpdateLeds (float k1, float k2)
{
    //led1 is red when recording, green when playing, off otherwise
    daisy::Color led1Color;
    led1Color.Init (daisy::Color::OFF);

    if (isCurrentlyRecording)
        led1Color.Init (daisy::Color::RED);
    else if (isCurrentlyPlaying)
        led1Color.Init (daisy::Color::GREEN);
    else if (isWaitingForInput)
        led1Color.Init (255, 255, 0); // yellow
    pod.led1.SetColor (led1Color);

#if ENABLE_ALL_EFFECTS
    //led 2 reflects the effect parameter
    pod.led2.Set (k2 * (curFxMode == 2), k2 * (curFxMode == 1), k2 * (curFxMode == 0 || curFxMode == 2));
#endif
    pod.UpdateLeds();
}

void ProcessControls()
{
    float k1, k2;
#if ENABLE_ALL_EFFECTS
    delayTarget = 0;
    feedback = 0;
    drywet = 0;
#endif

    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    UpdateButtons();
#if ENABLE_ALL_EFFECTS
    UpdateEffectKnobs (k1, k2);
    UpdateEncoder();
#endif
    UpdateLeds (k1, k2);
}

float GetLooperSample (daisy::AudioHandle::InterleavingInputBuffer in, size_t i)
{
    //this should basically only happen when we start and we don't have anything recorded
    if (! isCurrentlyRecording && ! isCurrentlyPlaying)
        return in[i] * (.968f - drywet);

    if (isCurrentlyRecording)
    {
        //old, overdub way of recording. How weird to have the current one at .5. Probably a cheap way to avoid clipping
        //looperBuffer[positionInLooperBuffer] = looperBuffer[positionInLooperBuffer] * 0.5 + in[i] * 0.5;
        looperBuffer[positionInLooperBuffer] = in[i];

        if (isFirstLoop)
            ++numRecordedSamples;
    }

    auto outputSample = looperBuffer[positionInLooperBuffer];

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
    if (! isCurrentlyRecording)
        outputSample = outputSample * drywet + in[i] * (.968f - drywet); //slider apparently only goes to .968f lol

    return outputSample;
}

void AudioCallback (daisy::AudioHandle::InterleavingInputBuffer  inputBuffer,
                    daisy::AudioHandle::InterleavingOutputBuffer outputBuffer,
                    size_t                                       numSamples)
{
#if ENABLE_INPUT_DETECTION
    if (! gotPreviousSample && numSamples > 0)
    {
        previousSample = inputBuffer[0];
        gotPreviousSample = true;
    }
#endif

    ProcessControls();

    float outputLeft, outputRight, inputLeft, inputRight;
    for (size_t curSample = 0; curSample < numSamples; curSample += 2)
    {
#if ENABLE_INPUT_DETECTION
        if (isWaitingForInput && std::abs (outputBuffer[curSample] - previousSample) > inputDetectionThreshold)
        {
            //TODO: setting currently playing here is needed to have GetLooperSample call ++positionInLooperBuffer, but i think we can probably use some other bool or rename this one
            isCurrentlyPlaying   = true;
            isCurrentlyRecording = true;
            isWaitingForInput    = false;
        }
#endif

        //get looper output
        const auto looperOutput { GetLooperSample (inputBuffer, curSample) };
        //TODO: support stereo looping here and when recording
        outputBuffer[curSample] = outputBuffer[curSample + 1] = looperOutput;

        //apply effects
        inputLeft = outputBuffer[curSample];
        inputRight = outputBuffer[curSample + 1];

#if ENABLE_ALL_EFFECTS
        switch (curFxMode)
        {
            case fxMode::delay: GetDelaySample (outputLeft, outputRight, inputLeft, inputRight); break;
            case fxMode::crush: GetCrushSample (outputLeft, outputRight, inputLeft, inputRight); break;
            case fxMode::reverb: GetReverbSample (outputLeft, outputRight, inputLeft, inputRight); break;
            default: outputLeft = outputRight = 0;
        }

        outputBuffer[curSample]     = outputLeft;
        outputBuffer[curSample + 1] = outputRight;
#endif
#if ENABLE_INPUT_DETECTION
        previousSample = outputBuffer[curSample];
#endif
    }
}

void RestoreLoopIfItExists()
{
    // Initialize the SDMMC Hardware. For this example we'll use: Medium (25MHz), 4-bit, w/out power save settings
    daisy::SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = daisy::SdmmcHandler::Speed::STANDARD;
    sdmmc.Init(sd_cfg);

    // Setup our interface to the FatFS middleware
    daisy::FatFSInterface::Config fsi_config;
    fsi_config.media = daisy::FatFSInterface::Config::MEDIA_SD;
    fsi.Init(fsi_config);
    FATFS& fs = fsi.GetSDFileSystem();

    // mount the filesystem to the root directory, and attempt to open the loop file
    bool loopWasLoaded = false;
    if (f_mount (&fs, "/", 0) == FR_OK)
    {
        if (f_open (&loopFile, loopFileName, FA_READ) == FR_OK)
        {
            //attempt to read up to the maxRecordingSize, but the actual bytes_read will tell us the length of the loop
            UINT bytes_read;
            const auto res = f_read (&loopFile, looperBuffer, maxRecordingSize, &bytes_read);
            if (res == FR_OK)
            {
                //we loaded the loop properly -- set our state as such
                loopWasLoaded       = true;
                cappedRecordingSize = bytes_read / sizeof (float);
                numRecordedSamples  = cappedRecordingSize;
                StopRecording();
            }
            else
            {
                pod.seed.PrintLine ("couldn't read looperBuffer from file!!");
            }
        }

        f_close (&loopFile);
    }

    //reset everything if we did not load a loop, either because there was none saved or because of another error
    if (! loopWasLoaded)
        ResetLooperState();
}

void saveLoop()
{
    //mount the file system and open the loop file for writing
    FATFS &fs = fsi.GetSDFileSystem();
    if (f_mount (&fs, "/", 0) == FR_OK)
    {
        auto res = f_open (&loopFile, loopFileName, (FA_CREATE_ALWAYS | FA_WRITE));
        if (res == FR_OK)
        {
            //everything opened fine, so write the stuff
            const auto bytesToWrite { cappedRecordingSize * sizeof (float) };
            UINT bytes_written;
            res = f_write (&loopFile, looperBuffer, bytesToWrite, &bytes_written);

            //and make sure it got written correctly
            if (res != FR_OK || bytesToWrite != bytes_written)
                pod.seed.PrintLine ("couldn't write looperBuffer to file!!");
        }
        else
        {
            pod.seed.PrintLine ("couldn't open file to write into it!!");
        }

        f_close (&loopFile);
    }
}

int main (void)
{
    //initialize pod hardware and logger
    pod.Init();
    pod.SetAudioBlockSize (4); // Set the number of samples processed per channel by the audio callback. Isn't 4 ridiculously low?
#if WAIT_FOR_SERIAL_MONITOR
    pod.seed.StartLog (true);
#else
    pod.seed.StartLog ();
#endif

    RestoreLoopIfItExists();

#if ENABLE_ALL_EFFECTS
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
#endif
    //start audio
    pod.StartAdc();
    pod.StartAudio (AudioCallback);

    bool led_state = true;
    while (1)
    {
        // blink the led
        pod.seed.SetLed (led_state);
        led_state = ! led_state;

        if (needToSave.load())
        {
            saveLoop();
            needToSave.store (false);
        }

        if (needToDelete.load())
        {
            f_unlink (loopFileName);
            needToDelete.store (false);
        }

        // PrintFloat (pod.seed, "dry wet", drywet, 3);

        daisy::System::Delay (1000);
    }
}
