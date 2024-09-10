/** Example of writing a "Hello World" to a text file on an SD Card 
 *  If the SD Card is present, and writing is successful the onboard LED will blink rapidly.
 *  Otherwise, the SD Card will blink once every two seconds.
 */
#include "daisy_seed.h"

#include "fatfs.h"

/** This prevents us from having to type "daisy::" in front of a lot of things. */
using namespace daisy;

#define TEST_FILE_NAME "SdCardWriteAndRead.txt"
#define TEST_FILE_CONTENTS \
    "This file is used for testing the Daisy breakout boards. Happy Hacking!"

/** Global Hardware access */
DaisySeed hw;

/** SDMMC Configuration */
SdmmcHandler sdmmc;
/** FatFS Interface for libDaisy */
FatFSInterface fsi;
/** Global File object */
FIL file;

int main(void)
{
    /** Initialize our hardware */
    hw.Init();
    hw.StartLog (true);

    /** Initialize the SDMMC Hardware 
     *  For this example we'll use:
     *  Medium (25MHz), 4-bit, w/out power save settings
     */
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::STANDARD;
    sdmmc.Init(sd_cfg);

    /** Setup our interface to the FatFS middleware */
    FatFSInterface::Config fsi_config;
    fsi_config.media = FatFSInterface::Config::MEDIA_SD;
    fsi.Init(fsi_config);

    /** Get the reference to the FATFS Filesystem for use in mounting the hardware. */
    FATFS& fs = fsi.GetSDFileSystem();

    /** default to a known error 
     *  by the end of the next if-statement it should be FR_OK
     */
    FRESULT res = FR_NO_FILESYSTEM;

    /** mount the filesystem to the root directory 
     *  fsi.GetSDPath can be used when mounting multiple filesystems on different media
     */
    if (f_mount (&fs, "/", 0) == FR_OK)
    {
        FixedCapStr<28> str = "Hello World while debugging!";

        if (f_open (&file, TEST_FILE_NAME, (FA_CREATE_ALWAYS | FA_WRITE))
            == FR_OK)
        {
            UINT bytes_written;
            res = f_write (&file, str.Cstr(), str.Size(), &bytes_written);
            f_close (&file);
        }

        char buff[2048];
        if (f_open (&file, TEST_FILE_NAME, FA_READ) == FR_OK)
        {
            UINT bytesread;
            res = f_read (&file, buff, str.Size(), &bytesread);
            if (res == FR_OK)
                hw.PrintLine ("managed to read this from the file: %s", buff);
            f_close (&file);
        }
    }

    /** Infinite Loop */
    while(1)
    {
        /** Very basic blink to indicate success or failure */
        uint32_t blink_rate;
        if(res == FR_OK)
            blink_rate = 125;
        else
            blink_rate = 1000;
        System::Delay(blink_rate);
        hw.SetLed(true);
        System::Delay(blink_rate);
        hw.SetLed(false);
    }
}
