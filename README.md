# Infinity Blade Trilogy for PS5

**Infinity Blade, Infinity Blade II and Infinity Blade III** as PS5 apps, running on
[Hydroid](https://github.com/ZiZc3/Hydroid) with the [ib-2-3-android](https://github.com/jelqedagain/ib-2-3-android) port.

60 fps, full controller support, the games' own movies and sound. Infinity Blade 1 runs on the
port for the first time, with the parts of iOS it was missing added here.

## The games

| Game | App | Your `.ipa` | Status |
|---|---|---|---|
| Infinity Blade | PPSA26383 | 1.4.3 (64-bit) | ✅ Playable |
| Infinity Blade II | PPSA26382 | 1.3.5 | ✅ Playable |
| Infinity Blade III | PPSA26381 | 1.4.4 | ✅ Playable |

Downloads: [Releases](../../releases).

## Features

- **60 fps** in all three games
- **Controller support** throughout: menus, fights, magic, cutscenes
- **Movies and sound** as in the original games
- **Fast first start**: the game is unpacked from your `.ipa` in under a minute; IB2 and IB3 read their big
  texture files straight from the `.ipa`, so they take less space
- **Sharper picture in Infinity Blade 1**: full-size weapon and armour textures, 8× texture filtering
- Textures converted once for the PS5 and kept, so areas load faster the next time

## Installing

1. Download the game's app from [Releases](../../releases) and unzip it.
2. Copy its folder to the PS5 homebrew folder:
   `/data/homebrew/`
3. Put **your own** `.ipa` of the game in the app's `game` folder:
   `/data/homebrew/<app>/game`
4. Start the game. The first start unpacks the game from the `.ipa`, then plays.

The apps don't include the games. Use the `.ipa` versions in the table.

## Controls

| Button | Action |
|---|---|
| Left stick | Move the cursor |
| ✕ | Tap / stab; Start on the title screen |
| Right stick | Swipe the sword in fights; look around |
| L1 / R1 | Dodge left / right |
| L2 or ○ | Block (hold) |
| □ | Magic |
| △ | Super move |
| D-pad | Magic slots, boss info |
| L3 (hold) | Fast-forward cutscenes |
| Options | Menu |

## What's inside

- **Infinity Blade II and III** use the ib-2-3-android port's own Android apps, unchanged.
- **Infinity Blade 1** uses a build of the same port with IB1's missing parts added: OpenAL sound,
  the old iOS movie player, its title screen, and picture settings. The source is in
  [`ib1-port/`](ib1-port), and the changes are listed in [`ib1-port/IB1-CHANGES.md`](ib1-port/IB1-CHANGES.md).
- **Hydroid** runs the Android port on the PS5. For these games it adds reading textures straight from
  the `.ipa`, native texture conversion, a movie player fix for loading screens, and 60 fps for IB2.

## Credits

- [ib-2-3-android](https://github.com/jelqedagain/ib-2-3-android) (MIT): the port that makes these games run
  on Android. Its license is included with each app.
- [Hydroid](https://github.com/ZiZc3/Hydroid): Android on the PS5.
- Infinity Blade, Infinity Blade II and Infinity Blade III are by Chair Entertainment and Epic Games.
  This project contains no game files.
