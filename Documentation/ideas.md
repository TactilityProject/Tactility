# TODOs

## Before release

## Higher Priority

- Notification sounds and vibration

## Medium Priority

- Check if main mesh task stack (8kB) can move to external RAM when available. Shift file IO to file_ops?
- SystemInfo is causing stack overflow when clicking on task to see its details
- Build P4 firmware flasher into main OS. Support firmware updates from Software app. Offer several versions/variants of the Wi-Fi firmware.
- SDL display dimming: T-Deck Plus Display settings allow to dim to 0 setting but display keeps minimum lighting. We have a hack in SDL display dimming to avoid dimming to a point that it shows no light. Fix discrepancy.
- Files app: Re-usable lvgl_files view with click/long click callbacks. Built-in swipe handling for pages.
- Sound settings: need API and UI for default playback and recording device selection
- saveBacklightDuty() and similar settings should be bound to a device
- "data" and "/data": Rename to "internal"? Find and get rid of hard-coding these names?
- Copy/paste buffer
- Add "Downloading Apps" step in Setup that downloads useful apps.
  It's an optional step at the end that only shows if wifi is connected.
- Improve watch interface. Consider adding some sort of device classification so UI in apps can choose to optimize for the watch.
- Update timezones.csv wrt 2026e, write tests for related code
- Try https://github.com/bootdsc/Fastwave-Eink-Driver with CL-32 e-paper.
- Pick different font to improve monochrome screen readability (or change the weight).
- Inspect code for memory improvements: replace `std::*` with `tt::std::*`
- ImageViewer: Support SVG files
- ImageViewer: Limit memory used to open an image.
  loadFile() now resizes fileData to the entire file size before displaying the image. start accepts a file path without a size limit, so opening a large image can exhaust memory on an embedded device before LVGL decodes it. Reject files above a supported limit or retain a bounded file-backed decoding path.
- A fullscreen tiny keyboard that works with single stroke gestures (similar to Palm OS Graffiti)
- Implement signal/raise for terminal apps (for CTRL+C and window resize signals)
- Build the simulator with `-fno-exceptions` to match ESP32. First make app exit() work without C++ unwinding there:
  scheduler.cpp's DeferredUnload relies on pthread_exit()'s forced unwind running its destructor.
  Without exceptions, an app that calls exit() is never unloaded and the next app start waits the full timeout.
- netutils-module: ping, wget, ifconfig, nslookup, etc.
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
- Try out speed optimizations: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
  (relates to CONFIG_ESP32S3_DATA_CACHE_LINE_64B that is in use for RGB displays via the `device.properties` fix/workaround)
- Default `gamma-curve` to 0 (the panel's own default) instead of 1 in the other display driver bindings
  (st7789-i8080, st7796, st7796-i8080, st7735, ili9341, jd9853). The st7789 one already does.
  Affects devices that don't set it: check them on hardware first and set `gamma-curve = <1>;` where the old look is needed.

## Lower Priority

- Auto-generate simulator profiles
- BlueTooth as radio backend for MeshCore/Meshtastic?
- Show device name clearly in on-device web server landing page (to identify tabs of multiple devices easier)
- When Wi-Fi connects, check for app updates
- lvgl-module has a keyboard.cpp that creates a `keyboard_group`. This group is set as the default group, so it can also work with trackball(= LVGL "encoder").
  Make a separate group that is the default group. The keyboard can then use it (or use its own).
  The basic idea is to invert the ownership: now the keyboard group is made the default group, but it's probably more logical to have the default group used by the keyboard.
- Explore LVGL9's FreeRTOS functionality
- Fix system time to not be 1980 (use build year january 1st as a minimum).
- Use std::span or string_view in StringUtils https://youtu.be/FRkJCvHWdwQ?t=2754 
- Mutex: Implement give/take from ISR support (works only for non-recursive ones)
- Show a warning screen if firmware encryption or secure boot are off when saving WiFi credentials.
- Calculator app should show regular text input field on non-touch devices that have a keyboard (Cardputer, T-Lora Pager)
- Make WiFi setup app that starts an access point and hosts a webpage to set up the device.
  This will be useful for devices without a screen, a small screen or a non-touch screen.

# App Ideas

- Weather app
- IR transceiver

