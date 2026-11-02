# Infinity Blade I-III for PS5

**Infinity Blade, Infinity Blade II and Infinity Blade III** PS5 port, running on
[Hydroid](https://github.com/ZiZc3/Hydroid) with the [ib-2-3-android](https://github.com/jelqedagain/ib-2-3-android) android port.

60 fps, full controller support.

| Game | App | Your `.ipa` |
|---|---|---|
| [Infinity Blade](https://github.com/ZiZc3/Infinity-Blade-PS5/releases/download/1.0.0/PPSA26383-Infinity-Blade.zip) | `PPSA26383-Infinity-Blade.zip` | 1.4.3 (64-bit) |
| [Infinity Blade II](https://github.com/ZiZc3/Infinity-Blade-PS5/releases/download/1.0.0/PPSA26382-Infinity-Blade-II.zip) | `PPSA26382-Infinity-Blade-II.zip` | 1.3.5 |
| [Infinity Blade III](https://github.com/ZiZc3/Infinity-Blade-PS5/releases/download/1.0.0/PPSA26381-Infinity-Blade-III.zip) | `PPSA26381-Infinity-Blade-III.zip` | 1.4.4 |

Downloads: [Releases](../../releases).

## Installing

1. Download the game's app from [Releases](../../releases) and unzip it.
2. Copy its folder to the PS5 homebrew folder:
   `/data/homebrew/`
3. Put **your own** `.ipa` of the game in the app's `game` folder:
   `/data/homebrew/<app>/game`
4. Start the game. The first start unpacks the game from the `.ipa`, then plays.

The apps don't include the games.

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

- **Infinity Blade II and III** use the ib-2-3-android port's own Android apps, unchanged.
- **Infinity Blade 1** uses a build of the same port with IB1's missing parts added: OpenAL sound,
  the old iOS movie player, its title screen, and picture settings. The source is in
  [`ib1-port/`](ib1-port), and the changes are listed in [`ib1-port/IB1-CHANGES.md`](ib1-port/IB1-CHANGES.md).
- **Hydroid** runs the Android port on the PS5. For these games it adds reading textures straight from
  the `.ipa`, native texture conversion, a movie player fix for loading screens, and 60 fps for IB1.

## Credits

- [ib-2-3-android](https://github.com/jelqedagain/ib-2-3-android) (MIT): the port that makes these games run
  on Android.
- [Hydroid](https://github.com/ZiZc3/Hydroid): Android on the PS5.
- Infinity Blade, Infinity Blade II and Infinity Blade III are by Chair Entertainment and Epic Games.
  This project contains no game files.
