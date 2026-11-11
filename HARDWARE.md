# Hardware contract

TinyBBS runs on the [Pocket-Dongle-S3](https://www.aliexpress.com/item/1005008896509314.html), an ESP32-S3 N16R8 board in a
USB-stick form factor, about 10 euros. It has a 0.96 inch ST7735 LCD and a
microSD slot on board and no exposed GPIO. Nothing is wired to it. Buy the
variant with the SD slot.

The pins below are the board's internal wiring for the LCD and SD slot,
proven first by TinyIRC, the earlier sketch. The firmware in `src/main.cpp`
uses the same assignments. Another ESP32-S3 board needs its own pin check.

## Target

| Item | Current target |
| --- | --- |
| Board | [Pocket-Dongle-S3](https://www.aliexpress.com/item/1005008896509314.html), ESP32-S3 N16R8 USB dongle with LCD and microSD |
| Configured board profile | `esp32-s3-devkitc1-n16r8`, 16 MB flash, 8 MB PSRAM |
| Framework | Arduino on the Espressif32 PlatformIO platform |
| Display | ST7735 colour TFT, native 80x160 pixels |
| Storage | microSD card over SD_MMC in 1-bit mode |
| Local control | BOOT button on GPIO 0 |
| Network | Isolated 2.4 GHz WiFi soft AP |
| Caller transports | HTTP browser terminal on port 80, Telnet on port 23 |

The connected board reported 16 MB flash and 8 MB embedded PSRAM during the
2026-11-11 flash. The dongle has no PlatformIO board of its own; the
`esp32-s3-devkitc1-n16r8` definition matches its module. Check a replacement
board before changing memory or flash settings. Do not assume an arbitrary
ESP32-S3 board has PSRAM.

## Pin map

| Function | GPIO | Firmware detail |
| --- | ---: | --- |
| TFT SCLK | 10 | SPI clock |
| TFT MOSI | 11 | SPI data to display |
| TFT CS | 12 | Display chip select |
| TFT DC | 13 | Data or command select |
| TFT RST | 14 | Display reset |
| SD_MMC D0 | 16 | 1-bit data line |
| SD_MMC CLK | 17 | SD clock |
| SD_MMC CMD | 18 | SD command line |
| BOOT / local UI input | 0 | `INPUT_PULLUP`, active low |

No other pins are used. There is no modem, Ethernet, audio output or
keyboard.

## Display

The firmware constructs the display as an ST7735 panel with an 80x160 native
framebuffer and the offsets used by the TinyIRC reference: 26, 1, 26, 1. It
then calls `setRotation(1)`, so the caller-facing and status layout is
landscape at 160x80 logical pixels.

The display uses Arduino GFX Library for Arduino. Do not replace its panel
offsets or colour order based only on a generic ST7735 wiring diagram. Cheap
80x160 boards often use different offsets and display a shifted image when
configured as a normal 128x160 panel.

## SD card

The card is mounted with `SD_MMC.begin("/sdcard", true)`. The second argument
selects 1-bit mode. The expected card format is FAT32.

TinyBBS mounts and writes its runtime records under `/BBS`. It creates needed
data, log and temporary directories if they are missing, but it does not
format the card. A failed mount leaves the BBS in a storage-fault state. Use a
known-good FAT32 card and copy the supplied BBS tree before diagnosing a
firmware problem.

The SD card carries caller-facing content and mutable board data. Remove power
only after the board is idle where possible. There is no measured power-loss
test result yet.

## WiFi and local network

The current firmware calls `WiFi.mode(WIFI_AP)`. It starts a soft AP using the
values in `/BBS/CONFIG/NETWORK.INI`:

| Setting | Default |
| --- | --- |
| SSID | `THE_BOARD` |
| Password | empty, open AP |
| Channel | 1 |
| AP address | `192.168.4.1` from the ESP32 AP default configuration |
| DNS | UDP port 53, wildcard response to the AP address |
| HTTP | TCP port 80 |
| Telnet | TCP port 23 |
| WiFi-client limit | 10, constrained by the ESP32 WiFi stack |
| BBS-node limit | 8 shared browser and Telnet sessions |

WiFi associations are not BBS nodes. A device can join the AP without opening
a terminal. A browser session receives a node only after it presses DIAL. The
firmware expires an unauthenticated login after two minutes, a stalled browser
carrier after three minutes and an authenticated idle session after 30
minutes.

`mode=ap_sta` and station credentials in `NETWORK.INI` are placeholders for a
future explicit bridge. They do not authorize LAN or Internet use. The 0.1
firmware logs that choice and starts AP-only mode.

## Power and USB

The dongle plugs straight into a USB charger or computer port. That is the
only power input. There is no battery circuit, measured current budget or
brownout qualification.

The USB connection is also normally used for flashing and serial diagnostics
at 115200 baud. Select the port assigned by the operating system. Do not copy
a hard-coded COM port from another machine.

### Flash-mode recovery note

The N16R8V's runtime image is QIO flash plus OPI PSRAM, but the proven direct
serial-flash mode is **DIO**. A direct `esptool` command that forces QIO can
cause the ROM to reset while loading the bootloader: Windows then makes a
repeating USB connect/disconnect sound and the serial log repeats
`rst:0x7 (TG0WDT_SYS_RST)` before application output.

This is recoverable. The ROM bootloader still accepts `esptool --chip esp32s3
--port COMx flash-id`. Rebuild with `platformio.flash.ini`, then rewrite the
bootloader at `0x0`, partitions at `0x8000`, `boot_app0.bin` at `0xe000` and
the application at `0x10000`, explicitly using `--flash-mode dio`. Verify the
hashes, reset, and check that `THE_BOARD` is visible again. Do not change the
board profile to a generic no-PSRAM board, and do not erase the full chip as a
first diagnostic step.

## Limits and verification

Eight active BBS nodes is the present firmware ceiling, not a completed
hardware benchmark. PlatformIO Core 6.2.0 compiles the current source with
the configured board profile. The 1.0 specification still calls for a
physical test of eight active BBS nodes and up to 16 associated WiFi clients.
There is no device test result that proves either number.

Before changing hardware or calling a build verified, test this sequence on
the physical board:

1. Cold boot with the prepared SD card.
2. LCD logo and status panel appear without a shifted display.
3. Join the AP and open `http://192.168.4.1`.
4. Connect a second client by Telnet to port 23.
5. Confirm both callers appear in WHO and can exchange chat or messages.
6. Reset and confirm the account and posted message are still present.

TinyIRC, kept in its own repository, is the fallback hardware reference if a
wiring or display regression appears.
