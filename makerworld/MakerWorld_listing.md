# Sports Ticker – Live Scores Desk Display (ESP32 CYD Stand)

**Short summary (for the listing header):**
A friendly little desk scoreboard that shows live scores for your favorite teams. A 2.8" ESP32 touchscreen sits in a rounded, penny-weighted stand. You pick your teams from your phone, and it updates on its own.

---

## The story

My roommate Lane lives and breathes Colorado sports. He's always checking his phone for the Broncos, the Avalanche and the CSU Rams, during dinner and even in the middle of a conversation. For his birthday I wanted to make him something that would just *sit there* and tell him the score.

So I built him his own little scoreboard.

It started with a cheap ESP32 "Cheap Yellow Display" (a 2.8" touchscreen with Wi-Fi built in) and a lot of evenings writing firmware. It pulls live scores from ESPN's free public data, so there's no account, no subscription and no app. When the Broncos have the ball, it draws a little to-scale field with the line of scrimmage, the first-down line and the live win probability. When his team scores, the whole screen erupts into a TOUCHDOWN (or GOAL!) animation in team colors.

Then it needed a body. I sketched a rounded screen "pod" sitting on a chunky, tapered base, and modeled it in Fusion 360 around a 3D model of the board:

- **Rounded and friendly.** Soft edges everywhere, and the bottom edge curves under so the base looks like it's floating. (From the back, the bolt holes and the cable hole make a surprised little face, and I'm keeping it.)
- **Penny-weighted.** The base has four round slots that each take a stack of 10 pennies flat. That's about 100 g of ballast, so it doesn't tip when you tap the screen. A lid hooks in on one side and clicks shut with a snap arm. No glue.
- **Dovetail mount.** Two rails on the back of the pod slide down into the base until a nub clicks, and then two M3 screws lock it.
- **Clean cable.** A low-profile 90° USB-C cable tucks into a pocket inside the pod and exits through a tunnel out the back of the base.
- **Heat-set inserts** hold everything together, so you can open it back up.

I printed it on my Bambu Lab X1C in matte PLA. Setting it up was *super easy*. You plug it in, scan the QR code on the screen to put it on your Wi-Fi, then scan it again to pick your teams on your phone. That's it.

Lane loved it. It lives on his desk now, and he hasn't had to pull his phone out during a game since. 🏈🏒

---

## Software

Firmware, phone setup page, the Fusion 360 stand generator and all print files: **https://github.com/ryancblocker/sports-ticker** (MIT license). Flashing takes one PlatformIO command; the README walks through it.

---

## Features (firmware)

- Live scores for any teams in 16 leagues: NFL, college football, NBA, WNBA, men's and women's college basketball, MLB, NHL, MLS, Premier League, La Liga, Bundesliga, Serie A, Ligue 1, Liga MX and the Champions League
- Live football field view: ball position, first-down line, current drive, down & distance, win probability, timeouts
- Live baseball: count, outs, bases. Hockey, basketball and soccer: last play
- Upcoming games show the day, time, countdown, TV channel and venue. Finals show the score by period and a W/L badge
- Full-screen scoring celebrations in team colors
- "My Teams" summary page; tap a team to jump to its game
- Hold anywhere to refresh. The header always shows when the scores were last updated
- Brightness slider, night dimming, dark and light themes
- Everything is configured from a phone page (QR code on the device); nothing to install

---

## Parts list

- **1× ESP32-2432S028R "Cheap Yellow Display"** (2.8" 320×240 touch). The case fits the 2-USB version (micro-USB + USB-C).
- **1× low-profile 90° USB-C cable.** The one I used: https://www.amazon.com/dp/B0CCJLS4R5. The plug sticks out about 5 mm from the port, and the pod has room for it.
- **1× USB power adapter, 5 V / 1 A or more.** Any phone charger works.
- **6× M3 heat-set inserts, 4.0 mm OD × 4 mm long.** 4 go in the pod front corners and 1 in each rail. The pilot holes are 3.6 mm.
- **6× M3 × 8 mm socket head cap screws.** 4 close the pod and 2 lock the pod to the base.
- **40× US pennies.** 10 per slot, about 100 g of ballast.
- **Matte PLA.** Bambu Studio shows the exact grams per plate.

**Tools:** a soldering iron with an insert tip (about 220 °C for PLA) and a 2.5 mm hex key.

---

## Printed parts and settings

Tested on a **Bambu Lab X1 Carbon**, Bambu Studio, matte PLA.

- **Pod Front:** print upright, standing on its edge (best finish on the screen face). Tree supports, build plate only.
- **Pod Back:** print upright, standing on its edge. Tree supports, build plate only (under the screw bosses).
- **Base:** print upright. No supports needed; the X1C bridges the tops of the penny slots. Add build-plate-only tree supports if yours sag.
- **Penny Lid:** print flat, snap arm up. No supports.

**Tips for a clean finish:** 0.12 mm layers or Variable Layer Height on the curved parts. Seam position set to *Back*, with scarf joint seams turned on. Outer wall speed around 150 mm/s.

---

## Assembly

1. **Inserts:** heat-press the 6 inserts flush, 4 into the Pod Front corner bosses and 1 into each rail on the Pod Back.
2. **Board:** set the CYD face-down into the Pod Front, with the pins through its mounting holes. Plug in the 90° USB-C cable and route it through the hole in the Pod Back.
3. **Close the pod:** add the Pod Back and screw it shut with 4 × M3×8. Snug, not tight.
4. **Weight:** drop 10 pennies flat into each of the four slots under the base. Hook the lid's two tabs into the pockets on one side, then swing the other end up until the snap **clicks**. To open it, lever a coin in the notch next to the snap.
5. **Mount:** feed the cable down through the base's tunnel, slide the pod's rails down into the base until the nub clicks, then lock it with 2 × M3×8 from the back.
6. **Power on:** scan the QR code to join Wi-Fi, then scan the next one to pick your teams. Done!

---

## Photos

Every photo comes in **4:3** (`_4x3`, web) and **3:4** (`_3x4`, mobile).

- **00 cover:** the bone white hero with the title and three feature pills. Use it as the MakerWorld cover image.
- **00 group:** three team colorways, each showing a different live sport: Broncos (navy and orange, football), Avalanche (burgundy and blue, hockey), Rockies (purple and silver, baseball). Print it in your team's colors!
- **01-06:** the stand in **bone white matte PLA**: hero, front, side profile, back, a live hockey game and a live baseball game.

*The screens in the renders show sample games. The device pulls real scores and logos from ESPN's public data. Team names and logos belong to their respective leagues and teams. This is a fan-made project and isn't affiliated with or endorsed by them.*
