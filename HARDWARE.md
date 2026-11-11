# Hardware contract

TinyBBS uses the ESP32-S3 layout proven by `reference/TinyIRC_2.ino`. The
firmware in `src/main.cpp` uses the same assignments. Treat these pins as a
contract. Changing them means making and testing a different hardware build.

## Target

| Item | Current target |
| --- | --- |
| Development board | Espressif ESP32-S3-DevKitC-1 (`esp32-s3-devkitc-1`) |
| Configured board profile | ESP32-S3-DevKitC-1-N8, 8 MB flash, no PSRAM |
| Framework | Arduino on the Espressif32 PlatformIO platform |
| Display | ST7735 colour TFT, native 80x160 pixels |
| Storage | microSD card over SD_MMC in 1-bit mode |
| Local control | BOOT button on GPIO 0 |
| Network | Isolated 2.4 GHz WiFi soft AP |
| Caller transports | HTTP browser terminal on port 80, Telnet on port 23 |

The PlatformIO board definition resolves to the N8, no-PSRAM profile above.
Check the module fitted to the physical DevKitC-1 before changing memory or
flash settings. If it differs, change the PlatformIO board configuration and
rebuild. Do not assume an arbitrary DevKitC-1 has PSRAM.

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

No other pins are part of the current TinyBBS hardware contract. In
particular, the project has no separate modem, Ethernet, audio-output or
physical keyboard wiring requirement.

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

Use the DevKitC-1's normal USB power connection with a reliable USB charger or
computer port. The repository does not define a separate 5 V input, battery
circuit, current budget or brownout qualification. Those depend on the exact
DevKitC-1 module, display board and SD card used.

The USB connection is also normally used for flashing and serial diagnostics
at 115200 baud. Select the port assigned by the operating system. Do not copy
a hard-coded COM port from another machine.

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

Keep `reference/TinyIRC_2.ino` unchanged during this work. It is the fallback
hardware reference if a wiring or display regression appears.
