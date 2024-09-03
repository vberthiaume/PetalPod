# Welcome to PetalPod FX Looper!

PetalPod is a looper with multi effects, based on the DaisyPod platform. [Here's a demo](https://www.youtube.com/watch?v=12vFhWRrvzs)!

# Setup on Windows
_This assumes you have a [daisy pod](https://electro-smith.com/products/pod) and the [ST Link](https://electro-smith.com/products/st-link-v3-mini-debugger) debugger_
- clone this repo and all its submodules: `git clone --recurse-submodules git@github.com:vberthiaume/PetalPod.git`
- download and install the daisy toolchain for windows from the link on this [page](https://github.com/electro-smith/DaisyWiki/wiki/1c.-Installing-the-Toolchain-on-Windows)
	- during the installation, add the tool gain to the `PATH` ***FOR THIS USER***, not all users
- install VS Code
- install the VS Code `Cortex Debug` extension
- make sure to select `Git Bash` as the default terminal profile in VS Code
- power up the daisy seed by connecting it to a usb power source
- connect the stm debugger to your computer with a usb-c cable
- open the repo's folder in VS code
- `ctrl+p`, `task build_all`
- `ctrl+p`, `task build_and_program`

# Controls:
| Control | Description | Comment |
| - | - | - |
| Button 1 | Hold to record from the line in input | |
| Button 2 | TBD! | |
| Knob 1 |  Wet/Dry control for the current effect | |
| Encoder |  Effect Selection | Reverb, Delay, and Bit-Crushing |
