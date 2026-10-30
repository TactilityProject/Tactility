# TODOs

## Before release

- Tactility/Source/service/autorotate/AutoRotate.cpp  
  TODO: Display settings app might be saving DisplaySettings at the same time. This might corrupt the settings file.

## Higher Priority

- Export platform-esp32 with TactilitySDK
- Find out why the first build of an app with a clean build cache folder always fails
- Improve posix support: use user home path and system /tmp folder.
- Drivers/audio-codec-module is not a module. Move it somewhere else. Or make it an actual module.
- Improve SPI kernel driver (implement read, write, transactions)
- External app loading: Check the version of Tactility and check ESP target hardware to check for compatibility
  Check during installation process, but also when starting (SD card might have old app install from before Tactility OS update)
- minitar/untarFile(): "entry->metadata.path" can escape its confined path (e.g. "../something")
- xl9555 and tca9534 drivers: support `GPIO_FLAG_ACTIVE_LOW` on outputs in software, like tca95xx does.
  Afterwards the raw level reset handling in cst66xx and tca8418 can use ACTIVE_LOW too.

## Medium Priority

- Improve watch interface. Consider adding some sort of device classification so UI in apps can choose to optimize for the watch.
- Support for running scripts at boot
- Update timezones.csv wrt 2026e, write tests for related code
- Notification support
- Low/critical memory warning test/rework
- Try https://github.com/bootdsc/Fastwave-Eink-Driver with CL-32 e-paper.
- Pick different font to improve monochrome screen readability (or change the weight).
- Inspect code for memory improvements: replace `std::*` with `tt::std::*`
- Tactility/Source/app/imageviewer/ImageViewer.cpp: Limit memory used to open an image.
  loadFile() now resizes fileData to the entire file size before displaying the image. start accepts a file path without a size limit, so opening a large image can exhaust memory on an embedded device before LVGL decodes it. Reject files above a supported limit or retain a bounded file-backed decoding path.
- A fullscreen tiny keyboard that works with single stroke gestures (similar to Palm OS Graffiti)
- Move more window management code from Tactility/ to window-manager
- Implement signal/raise for terminal apps (for CTRL+C and window resize signals)
- Build the simulator with `-fno-exceptions` to match ESP32. First make app exit() work without C++ unwinding there:
  scheduler.cpp's DeferredUnload relies on pthread_exit()'s forced unwind running its destructor.
  Without exceptions, an app that calls exit() is never unloaded and the next app start waits the full timeout.
- netutils-module: ping, wget, ifconfig, nslookup, etc.
- Core2: support power off via software
- Make it clearer to end-users that an SD card is required to run Tactility
- Warn if file operations are done from prohibited tasks (e.g. lvgl task)
- stopAppFromToolbar() in lvgl/Lvgl.cpp stops the top-most app. Change it so the toolbar knows for which app id it is created, so it can rely on that.
- `struct Driver` has an `.owner`, but it's not always set. Either validate on Module construct that it matches, or otherwise set it during module start. The problem: NULL parent currently means that driver is not removable. This clashes with setting it dynamically. Consider some kind of flag to determine removability.
- Consider moving certain drivers into separate modules: audio, bt, wifi, etc
- Consider using https://github.com/Graphify-Labs/graphify
- Make a URL handler. Use it for handling local files. Match file types with apps.
  Create some kind of "intent" handler like on Android.
  The intent can have an action (e.g. view), a URL and an optional bundle.
  The manifest can provide the intent handler
- Core2: support power off via software
- Try out speed optimizations: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
  (relates to CONFIG_ESP32S3_DATA_CACHE_LINE_64B that is in use for RGB displays via the `device.properties` fix/workaround)

## Lower Priority

- BlueTooth as radio backend for MeshCore/Meshtastic?
- Show device name clearly in on-device web server landing page (to identify tabs of multiple devices easier)
- When Wi-Fi connects, check for app updates
- Diceware app has large "+" and "-' buttons on Cardputer. It should be smaller.
- lvgl-module has a keyboard.cpp that creates a `keyboard_group`. This group is set as the default group, so it can also work with trackball(= LVGL "encoder").
  Make a separate group that is the default group. The keyboard can then use it (or use its own).
  The basic idea is to invert the ownership: now the keyboard group is made the default group, but it's probably more logical to have the default group used by the keyboard.
- Localize all apps
- Explore LVGL9's FreeRTOS functionality
- Fix system time to not be 1980 (use build year january 1st as a minimum).
- Use std::span or string_view in StringUtils https://youtu.be/FRkJCvHWdwQ?t=2754 
- Mutex: Implement give/take from ISR support (works only for non-recursive ones)
- Show a warning screen if firmware encryption or secure boot are off when saving WiFi credentials.
- Calculator app should show regular text input field on non-touch devices that have a keyboard (Cardputer, T-Lora Pager)
- Make WiFi setup app that starts an access point and hosts a webpage to set up the device.
  This will be useful for devices without a screen, a small screen or a non-touch screen.

# Nice-to-haves

- Audio player app
- Audio recording app
- OTA updates

# App Ideas

- Map widget:
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/application/ui/ui_geomap.cpp
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/tools/generate_world_map.bin.py
  https://github.com/portapack-mayhem/mayhem-firmware/releases
- Weather app: https://lab.flipper.net/apps/flip_weather
- wget app: https://lab.flipper.net/apps/web_crawler (add profiles for known public APIs?)
- Chip 8 emulator
- Discord bot
- IR transceiver app
- GPS app
- Investigate CSI https://stevenmhernandez.github.io/ESP32-CSI-Tool/
- Calendar
- RSS reader
