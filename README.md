SAPPL: scrcpy Application Launcher

Features:
- Auto connection USB/WiFi with USB priority
- One-click Setup WiFi connection
- Launch mobile apps separately
- Keep using mobile apps with dimmed screen

Required:
- [scrcpy](https://github.com/Genymobile/scrcpy/releases) (included scrcpy-win64-v4.1)

How to use :
1. Download and extract [sappl main.zip](https://github.com/satriamjati/sappl/archive/refs/heads/main.zip) (or see Portable below)
1. Enable USB debugging on your android
2. Connect android to  pc via USB
3. Open ``sappl.exe``
4. Wait for refresh
5. Click ``Set WiFi``
6. Disconnect android
7. Launch your apps by double click

Portable:
- Download ``sappl.exe`` 
- Save it anywhere (requires `scrcpy` on path environment) or copy to `scrcpy` root directory

Notes:
- ``Refresh`` only needed if some apps are not shown (you may need unlock screen)
- ``KA Reset`` if your phone screen physically on, or changing connection (USB/WiFi)
- ``KA Off`` to turn off anti-sleep feature 
- ``fUSB FT``  to force usb connection if both usb and wifi connected, othweise app will not launched
- ``Set WiFi`` to setup wifi connection

Tested on scrcpy 4.1 using android 14