# Infinity Blade 1 on the ib-2-3-android port

This folder is the [ib-2-3-android](https://github.com/jelqedagain/ib-2-3-android) port (MIT) with the
additions Infinity Blade 1 needs. The port was made for Infinity Blade II and III; IB1 is an older
Unreal Engine 3 game that uses parts of iOS the port did not have. Based on upstream commit `307725e`.

Every change is for IB1 only: the game is recognised by its bundle id (`com.chairentertainment.birthright`,
`game::is_ib1()`), and IB2 / IB3 run exactly as before.

| Area | File | What it does |
|---|---|---|
| Sound | `src/audio/openal.cpp` | OpenAL (the 25 `al` / `alc` functions IB1 uses), mixed into the port's audio output with gain, pitch, looping, 3D pan and distance fade. IB1 waited forever for an audio device without it. |
| Movies | `src/game/ib1.cpp` | `MPMoviePlayerController` (IB1's movie player) on the port's video playback, with its playback state and finish notifications |
| Title screen | `src/game/ib1.cpp`, `src/gles/gl.cpp` | IB1's "Start" picture (a UIImageView the port doesn't draw) blended over the logo movie, and pressed with the controller (✕ / Options) |
| Picture | `src/game/ib1.cpp` | IB1's config served with every texture group at full size (weapons and vehicles were 1/8 and 1/4 on every Apple device) and 8× anisotropic filtering; same-length edits only |
| Frame rate | `src/game/config.cpp` | 60 fps (IB1 capped itself at 35) |
| IB2 / IB3 features | `src/main.cpp` | the save editor, ClashMob, touch zones and other IB2 / IB3 features stay off for IB1 (they were made for those games' objects) |
| Small parts | `src/game/ib1.cpp` | `UIAccelerometer` (present, quiet), `CGAffineTransformTranslate`, `glGetBooleanv(GL_SHADER_COMPILER)` |

Building: as the port (`build-android.sh`, Android NDK). The Windows-only ANGLE DLLs in `external/angle`
are not included here.
