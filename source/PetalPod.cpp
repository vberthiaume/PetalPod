//I shouldn't need to include this, but not having it causes vscode to think it's missing
#define USE_DAISYSP_LGPL 1

#include "daisysp.h"
#include "helper.hpp"
#include "fatfs.h"

#include <atomic>

daisy::DaisyPod pod;

//looper things
constexpr auto maxRecordingSize     = 48000 * 60 * 1; // 1 minute of floats at 48 khz.
constexpr auto fadeOutLength        = 1000;           // the number of samples at the end of the loop where we apply a linear fade out
bool           isFirstLoop          = true;           // the first loop will set the length for the buffer
bool           isWaitingForInput    = false;
bool           isCurrentlyRecording = false;
bool           isCurrentlyPlaying   = false;

float DSY_SDRAM_BSS looperBuffer[maxRecordingSize]; //DSY_SDRAM_BSS means this buffer will live in SDRAM, see https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
int                 positionInLooperBuffer = 0;
int                 cappedRecordingSize    = maxRecordingSize;
int                 numRecordedSamples     = 0;

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

//input detection
bool           gotPreviousSample       = false;
float          previousSample          = -1000.f;
constexpr auto inputDetectionThreshold = .025f;

//file saving
std::atomic<bool> needToSave { false };
constexpr const char* loopFileName { "savedLoop.wav" };

#define TEST_FILE_NAME "SdCardWriteAndRead.txt"
#define TEST_FILE_CONTENTS \
    "This file is used for testing the Daisy breakout boards. Happy Hacking!"

/** Global Hardware access */
daisy::DaisySeed hw;

/** SDMMC Configuration */
daisy::SdmmcHandler sdmmc;
/** FatFS Interface for libDaisy */
daisy::FatFSInterface fsi;
/** Global File object */
FIL file;

void ResetLooperState()
{
    isWaitingForInput      = false;
    isCurrentlyPlaying     = false;
    isCurrentlyRecording   = false;
    isFirstLoop            = true;
    positionInLooperBuffer = 0;
    numRecordedSamples     = 0;

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

void FadeOutLooperBuffer()
{
    const auto diff          = cappedRecordingSize - fadeOutLength;
    const auto actualFadeOut = (diff > fadeOutLength) ? fadeOutLength : cappedRecordingSize;
    for (int i = cappedRecordingSize; i > cappedRecordingSize - actualFadeOut; --i)
    {
        //map i values to a ramp that goes from 0 to 1
        const auto ramp = jmap (static_cast<float> (i), static_cast<float> (cappedRecordingSize),
                                static_cast<float> (cappedRecordingSize - actualFadeOut), 0.f, 1.f);

        //can't print from the audio thread/callback
        // PrintFloat (pod.seed, "ramp", ramp, 2);

        //ramp out looper buffer
        looperBuffer[i] *= ramp;
    }
}

void StopRecording()
{
    //stop recording, set the loop length and fade out buffer
    isCurrentlyRecording = false;
    isFirstLoop          = false;
    cappedRecordingSize  = numRecordedSamples;
    numRecordedSamples   = 0;
}

void UpdateButtonsOldRecordWhileHolding()
{
    //button 1 just started to be held button: start recording
    if (pod.button1.RisingEdge())
    {
        isCurrentlyPlaying   = true;
        // isCurrentlyRecording = true;
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

            FadeOutLooperBuffer();
        }

        isCurrentlyRecording = false;
    }
}

void DeleteSavedFile()
{

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
                isWaitingForInput = true;
            }
            //button 1 pressed for the second time
            else
            {
                //we never detected any input so we didn't record anything
                if (isWaitingForInput)
                {
                    isWaitingForInput = false;
                }
                //we did record something
                else
                {
                    StopRecording();
                    FadeOutLooperBuffer();
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
    if (pod.button1.TimeHeldMs() >= 1000)
    {
        ResetLooperState();
        DeleteSavedFile();
    }
}

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

    //pusing the encoder saves the current loop to file
    if (pod.encoder.RisingEdge())
        needToSave.store (true);
}

void UpdateLeds (float k1, float k2)
{
    //led1 is red when recording, green when playing, off otherwise
    daisy::Color led1Color;
    led1Color.Init (daisy::Color::OFF);

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
    if (! gotPreviousSample && numSamples > 0)
        previousSample = inputBuffer[0];

    ProcessControls();

    float outputLeft, outputRight, inputLeft, inputRight;
    for (size_t curSample = 0; curSample < numSamples; curSample += 2)
    {
        if (isWaitingForInput && std::abs (outputBuffer[curSample] - previousSample) > inputDetectionThreshold)
        {
            //TODO: setting currently playing here is needed to have GetLooperSample call ++positionInLooperBuffer, but i think we can probably use some other bool or rename this one
            isCurrentlyPlaying   = true;
            isCurrentlyRecording = true;
            isWaitingForInput    = false;
        }

        //get looper output
        const auto looperOutput { GetLooperSample (inputBuffer, curSample) };
        //TODO: support stereo looping here and when recording
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

        previousSample = outputBuffer[curSample];
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

    // mount the filesystem to the root directory. fsi.GetSDPath() can be used when mounting multiple filesystems on different media
    bool needToReset = true;
    if (f_mount (&fs, "/", 0) == FR_OK)
    {
        //if loopFileName exists
        if (f_open (&file, loopFileName, FA_READ) == FR_OK)
        {
            //and we successfully read its content into looperBuffer
            UINT bytes_read;
            //TODO: LOL, we need to save the cappedRecordingSize somewhere, otherwise after a restart it'll have the default value
            auto res = f_read (&file, looperBuffer, cappedRecordingSize, &bytes_read);
            if (res == FR_OK)
            {
                //set our state correctly as being fully loaded
                needToReset = false;
                numRecordedSamples = bytes_read;
                StopRecording();
            }

            f_close (&file);
        }
    }

    //reset everything if there was no saved file, or there was an error
    if (needToReset)
        ResetLooperState();
}

void saveLoop()
{
    FATFS &fs = fsi.GetSDFileSystem();
    if (f_mount (&fs, "/", 0) == FR_OK)
    {
        auto res = f_open (&file, loopFileName, (FA_CREATE_ALWAYS | FA_WRITE));
        if (res == FR_OK)
        {
            UINT bytes_written;
            res = f_write (&file, looperBuffer, cappedRecordingSize, &bytes_written);
            if (res != FR_OK)
                pod.seed.PrintLine ("couldn't write to file!!");

            f_close (&file);
        }
        else
            pod.seed.PrintLine ("couldn't open file to write into it!!");
    }
}

int main (void)
{
    //initialize pod hardware and logger
    pod.Init();
    pod.SetAudioBlockSize (4); // Set the number of samples processed per channel by the audio callback. Isn't 4 ridiculously low?
    pod.seed.StartLog ();

    RestoreLoopIfItExists();

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

        // PrintFloat (pod.seed, "dry wet", drywet, 3);

        daisy::System::Delay (1000);
    }
}
