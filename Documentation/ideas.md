# TODOs

## Before release

- Tactility/Source/service/autorotate/AutoRotate.cpp  
  TODO: Display settings app might be saving DisplaySettings at the same time. This might corrupt the settings file.
- Try out speed optimizations: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
  (relates to CONFIG_ESP32S3_DATA_CACHE_LINE_64B that is in use for RGB displays via the `device.properties` fix/workaround)

## Higher Priority

- CrashDiagnostics shouldn't show a QR when there's no callstack
- stopAppFromToolbar() in Tactility.cpp stops the top-most app. Change it so the toolbar knows for which app id it is created, so it can rely on that.
- Warn if file operations are done from prohibited tasks (e.g. lvgl task)
- Move USB host task stacks to SPIRAM when available: esp32_usbhost*.cpp
- Get rid of WiFi service (Wifi.cpp/h) in Tactility.cpp
- Make it more clear to end-users that an SD card is required to run Tactility
- Add bold fonts for e-ink readability improvement
- Improve Setup: Show "Step done" screen
- Improve Setup: Add keyboard/keypad navigation explanation
- Improve kernel_init.cpp (and other modules): create driver_ensure_added() and driver_ensure_destructed()
- Drivers/audio-codec-module is not a module. Move it somewhere else. Or make it an actual module.
- LilyGO T-Dongle S3: 1 button control, stop auto-launching web server
- Core2: support power off via software
- Create `#define` for empty module (for modules that fully rely on device.properties and don't define drivers or have start/stop logic)
- Improve SPI kernel driver (implement read, write, transactions)
- Add font design tokens such as "regular", "title" and "smaller". Perhaps via the LVGL kernel module.
- TCA9534 keyboards should use interrupts
- External app loading: Check the version of Tactility and check ESP target hardware to check for compatibility
  Check during installation process, but also when starting (SD card might have old app install from before Tactility OS update)
- Make a URL handler. Use it for handling local files. Match file types with apps.
  Create some kind of "intent" handler like on Android.
  The intent can have an action (e.g. view), a URL and an optional bundle.
  The manifest can provide the intent handler
- Support direct installation of an `.app` file with `tactility.py install helloworld.app <ip>`
- Support `tactility.py target <ip>` to remember the device IP address.
- minitar/untarFile(): "entry->metadata.path" can escape its confined path (e.g. "../something")

## Medium Priority

- lvgl-module's spinner relies on hard-coded spinner asset from Tactility main project.
- esp_lvgl_port settings has a large stack size (~9kB) to fix stackoverflow when LVGL events (e.g. button click) do actions like file operations do actions like file operations. Can we reduce the callstack?
- `struct Driver` has an `.owner`, but it's not always set. Either validate on Module construct that it matches, or otherwise set it during module start. The problem: NULL parent currently means that driver is not removable. This clashes with setting it dynamically. Consider some kind of flag to determine removability.
- Consider moving certain drivers into separate modules: audio, bt, wifi, etc
- Consider using https://github.com/Graphify-Labs/graphify
- Consider implementing LVGL gridnav in apps https://lvgl.io/docs/open/9.3/details/auxiliary-modules/gridnav.html
- Make USB host driver disabled by default, so it doesn't consume memory
- TactilityTool: Make API compatibility table (and check for compatibility in the tool itself)
- Bug: Crash handling app cannot be exited with an EncoderDevice. (current work-around is to manually reset the device)
- Refactor HttpServer into C code and move implementation to http-module
- Use GPS time to set/update the current time

## Lower Priority

- Diceware app has large "+" and "-' buttons on Cardputer. It should be smaller.
- lvgl-module has a keyboard.cpp that creates a `keyboard_group`. This group is set as the default group, so it can also work with trackball(= LVGL "encoder").
  Make a separate group that is the default group. The keyboard can then use it (or use its own).
  The basic idea is to invert the ownership: now the keyboard group is made the default group, but it's probably more logical to have the default group used by the keyboard.
- Localize all apps
- Support hot-plugging SD card (note: this is not possible if they require the CS pin hack)
- Explore LVGL9's FreeRTOS functionality
- Consider using non_null (either via MS GSL, or custom)
- Fix system time to not be 1980 (use build year as a minimum). Consider keeping track of the last known time.
- Use std::span or string_view in StringUtils https://youtu.be/FRkJCvHWdwQ?t=2754 
- Mutex: Implement give/take from ISR support (works only for non-recursive ones)
- Show a warning screen if firmware encryption or secure boot are off when saving WiFi credentials.
- Calculator app should show regular text input field on non-touch devices that have a keyboard (Cardputer, T-Lora Pager)
- Allow for WSAD keys to navigate LVGL (this is extra nice for cardputer, but just handy in general)
- Create a "How to" app for a device. It could explain things like keyboard navigation on first start.
- Make WiFi setup app that starts an access point and hosts a webpage to set up the device.
  This will be useful for devices without a screen, a small screen or a non-touch screen.

# Nice-to-haves

- Considering the lack of callstack debugging for external apps: allow for some debugging to be exposed during a device crash. Apps could report their state (e.g. an integer value) which can be stored during app operation and retrieve after crash. The same can be done for various OS apps and states. We can keep an array of these numbers to keep track of the last X states, to get an idea of what's going on.
- Audio player app
- Audio recording app
- OTA updates
- If present, use LED to show boot/wifi status

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
