# Sports Ticker 🏈🏀⚾🏒⚽

A birthday-gift desk scoreboard: an **ESP32-2432S028** ("Cheap Yellow Display", 2.8" touch screen) that shows live scores for your favorite teams, pulled from ESPN's free public API. Teams and settings are picked from a phone.

```
sports-ticker/
├── firmware/                  PlatformIO project (Arduino framework)
│   ├── src/                   display, ESPN client, UI, web server
│   ├── web/index.html         phone config page (embedded into firmware at build)
│   ├── web/teams.json         snapshot of every team in 16 leagues
│   ├── fonts/                 Inter + Anton (OFL) source fonts
│   └── tools/                 embed_web.py (build step), fetch_teams.py (refresh teams),
│                              make_fonts.py (regenerate the smooth fonts)
└── case/SportsTickerStand/    Fusion 360 script → screen pod + weighted base (+ STL export)
```

## What it does

- **Team cards**: real team logos (downloaded once from ESPN and cached on the board), big scores, records, his team marked with an accent bar.
  - **Live football**: a to-scale field with end zones in team colors, ball on the line of scrimmage, yellow first-down line, this drive shaded, down & distance, live win probability, drive summary, and timeouts left.
  - **Live baseball**: count, outs, bases, last play. **Hockey / basketball / soccer**: last play.
  - **Upcoming**: day, time, countdown, TV channel, venue. **Final**: score by period plus a W/L badge.
- **My Teams summary page** (4 teams per page). Pages auto-rotate; while anything is live, only live games rotate.
- **Score alerts**: full-screen TOUCHDOWN / FIELD GOAL / GOAL / RUN SCORES animation in team colors, plus the board LED. An optional banner when the opponent scores (basketball is skipped because it scores too often).
- **Animated boot screen** (Wi-Fi → clock → scores, with a progress bar), a pulsing LIVE dot, dark or light theme, night dimming.
- **Phone settings** at the ticker's address, in three tabs: **Teams** (pick from 16 leagues), **Display** (live preview; theme, accent, logo size, what to show, rotation, alerts, brightness), **Device** (time zone, demo, maintenance).

**Touch:** tap the left third of the screen for the previous page, anywhere else for the next. **Hold** anywhere to refresh: the header counts down 3 seconds (let go to cancel), then shows a spinner until fresh scores arrive. The header always shows when the scores were last updated. Tap the **gear** in the bottom-right corner for the info screen: a QR code for the phone settings page, the address, and a **brightness bar**; close it with the **✕** in the top-right corner. On **My Teams**, tap a team to jump to its game. **Hold** on the info screen to start the **demo of every screen** (it also starts from Device → *Start demo on ticker*); in the demo, tap to step through screens and hold to exit. **Hold while powering on** to recalibrate touch.

## 1. Flash the firmware

Needs [PlatformIO](https://platformio.org/) (`brew install platformio`). Plug the CYD in by USB:

```bash
cd firmware
```

```bash
pio run -e cyd -t upload && pio device monitor
```

If the screen is white or the colors look like a negative, you have the 2-USB board revision. Flash `-e cyd2usb` instead, or tick **Invert colors** on the config page.

## 2. First boot (do this before you wrap it)

1. **Touch calibration:** tap the arrows in each corner.
2. **Wi-Fi:** the screen shows a QR code. Scan it (or join `SportsTicker-Setup`), pick the Wi-Fi network, and enter its password.
3. **Pick teams:** on a phone on the same Wi-Fi, scan the QR code on the screen or open the address it shows (or **http://sportsticker.local**). Add teams, then tap **Save to ticker**.

Your friend will have to redo the Wi-Fi step on their network. That's the same QR flow, and teams and settings are kept. To start the hotspot again from the config page: **Device → Forget Wi-Fi**.

## 3. Print the stand

The stand is a rounded screen pod on a weighted, tapered base (`case/SportsTickerStand/SportsTickerStand.py`). It's built for the **2-USB CYD** with a low-profile 90° USB-C cable. The board model it was checked against is in `case/reference/` (Havenview CYD CAD from the community CYD repo).

1. In Fusion: **Utilities → Add-Ins → Scripts and Add-Ins → +**, add `case/SportsTickerStand`, then **Run**. It builds a new design (with a keep-out model of the board), checks for collisions, and writes STLs to `case/stl/`. Sizes are constants at the top of the script.
2. Print (PLA/PETG, 0.2 mm). STLs come out in their assembled position, so lay each one flat in the slicer:
   - **Pod Front**: face down. **Pod Back**: open side down (rails up).
   - **Base**: upright. The X1C bridges the tops of the four penny bores fine; add tree supports (build plate only) if yours sag.
   - **Penny Lid**: flat, snap arm up. `test-print/SportsTicker_BaseAndLid.3mf` has just these two parts for a reprint.
3. Hardware: 6× M3 heat-set inserts, 4.0 mm OD × 4 mm long (4 in the pod front corners, 1 in each rail; pilot holes are 3.6 mm), 4× M3×8 (pod shell), 2× M3×8 (pod to base), 40 pennies (10 per bore, about 100 g). No glue needed.
4. Assembly:
   - Plug the 90° USB-C cable into the board.
   - Set the board on the pins in the pod front, then lay the cable across the back toward the hole at the bottom centre.
   - Screw on the pod back.
   - Feed the cable's far end through the tunnel in the base, from the front opening out the back.
   - Slide the pod's rails down into the base's slots until the nub clicks, then bolt it from the back.
   - Drop 10 pennies flat into each of the four bores. Hook the lid's two tabs into the pockets on the left side, then swing the right end up until the snap clicks. To open, lever a coin in the notch next to the snap.

Outside size: **105 W × 66 D × 98 H mm**, screen tilted 12° back.

## How the data works

ESPN's site API is free and needs no key (it's unofficial, so it could change someday):

| Call | When | Size |
|---|---|---|
| `…/{sport}/{league}/teams/{id}` | every 30 min per team | ~21 KB → record + next game id |
| `…/{sport}/{league}/scoreboard/{eventId}` | every **15 s** while live, 30 s near kickoff, 15 min otherwise | ~17 KB → score, clock, situation |

Responses are streamed through an ArduinoJson filter, so RAM use stays small. Fetching runs on core 0, so the touch screen never freezes. A final score stays up for 4 hours before the card moves on to the next game.

**Performance notes.** The ESP32 has no PSRAM, so a full 320×240 frame buffer (150 KB) can't coexist with HTTPS (~45 KB). Frames are rendered in three 80-row strips into a 51 KB sprite and pushed over 40 MHz SPI: about 25–30 ms per full redraw. Small animations (spinner, LIVE pulse, celebration rings) run at 20–30 fps. Logos are decoded from PNG once, at download time, into raw pixels stored in 32 flash slots (no filesystem: a descriptor bug in the ESP32 network stack kept closing open LittleFS files), because the PNG decoder needs ~45 KB and would starve HTTPS if it ran on every draw. Each team's logos are fetched as soon as its game data arrives, and the boot screen waits for them, so placeholders rarely show.

ESPN's `/teams` list endpoint doesn't allow browser (CORS) requests, so the phone gets the team list from the ticker itself. To refresh it (new season, expansion teams):

```bash
python3 firmware/tools/fetch_teams.py
```

Then reflash.

## Hardware notes (ESP32-2432S028R)

| Function | Pins |
|---|---|
| Display (ILI9341, HSPI) | SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21 |
| Touch (XPT2046) | SCLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| RGB LED (active low) | R 4, G 16, B 17 |

## Credits & license

- Code, case design and print files: [MIT License](LICENSE).
- Board model in `case/reference/`: the Havenview CYD CAD from [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display), used as a fit reference.
- Fonts: [Inter](https://rsms.me/inter/) and [Anton](https://fonts.google.com/specimen/Anton), both under the SIL Open Font License (see `firmware/fonts/`).
- Scores and logos come from ESPN's public endpoints at runtime. This is a fan project, not affiliated with ESPN or any league or team.
