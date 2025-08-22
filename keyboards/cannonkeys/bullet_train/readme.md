# Bullet Train

A Low Profile V4N4GON PCB with breakaway num row

The hotswap and solderable editions both use this firmware.
Hotswap supports Gateron KS-33 switches.
Solderable supports both Gateron KS-33 and Kailh Choc v2 switches.

* Keyboard Maintainer: [Andrew Kannan](https://github.com/awkannan)  
* Hardware Supported: RP2040 
* Hardware Availability: [CannonKeys](https://cannonkeys.com) 

Make example for this keyboard (after setting up your build environment):

    make cannonkeys/bullet_train:default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).


## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key at (0,0) in the matrix (usually the top left key or Escape) and plug in the keyboard
* **Physical reset button**: Hold the bootmode button down and hit the reset button. Or double tap the reset button quickly while the boot switch is set to "0".
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available
