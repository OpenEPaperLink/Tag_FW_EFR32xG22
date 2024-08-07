- Implement infoscreens (app, ~~display~~)
  - ~~INFOSCREEN_DEEPSLEEP (displayed when entering deepsleep)~~
  - ~~INFOSCREEN_BOOT (displayed when booting unless disabled by NVM setting)~~
  - ~~INFOSCREEN_BOOT_FOUND_AP (displayed when seeing an AP after hard reset)~~
  - ~~INFOSCREEN_LONG_SCAN (displayed when not seeing an AP after hard reset)~~
  - INFOSCREEN_LOST_CONNECTION (displayed when losing connection)
    - Replaced by overlay?
  - ~~INFOSCREEN_FWU (displayed when starting FWU)~~
  - ~~INFOSCREEN_WAKEUP_BUTTON1 (displayed when button 1 is pressed and corresponding image is present)~~
  - ~~INFOSCREEN_WAKEUP_BUTTON2 (displayed when button 2 is pressed and corresponding image is present)~~
  - ~~INFOSCREEN_WAKEUP_GPIO (displayed when GPIO is asserted and corresponding image is present)~~
  - ~~INFOSCREEN_WAKEUP_NFC (displayed when NFC is scanned and corresponding image is present)~~
  - INFOSCREEN_WAKEUP_RFWAKE (displayed when RF wake is triggered and corresponding image is present)
- ~~Implement more drivers (display)~~
- Asyncify display draw logic (app, display)
  - Unsure whether this is worth it, it creates potential pitfalls in synchronisation
- ~~Create 'common' async EPD driver~~
- ~~Update flash reads to direct instead of through bootloader (nvm)~~
- ~~If orphaned, start out-of-cycle scan on NFC wake / button press (app, radio)~~
- Implement slideshow mode (app)
- Implement RF wake mode (low priority) (app + radio)
- Implement tag settings
  - Fixed channel (radio)
  - ~~Fast boot (app)~~
  - ~~Checkin interval (radio)~~
  - ~~Roaming (radio)~~
  - ~~Scan on orphan (radio)~~
  - ~~UI overlay (display)~~
  - Custom mode (app)
  - ~~Low battery overlay (app)~~
- ~~Implement firmware version suffix~~
- Implement dynamic tag declaration
- Implement deepsleep wake cause detection (currently treated as reboot)
- ~~Update readme~~
- ~~Figure out why power consumption in sleep is so high~~
  - ~~Figure out how the FD pin can be setup to wake us from EM4~~
- Implement NFC (pending TNB132M config documentation)
- Get rid of CPP runtime
  - Not worth it?
