# Audio

Like everything else in COMMANDERS, the sound is generated in code: the repository contains **no audio files**. Every sound effect is synthesised once at startup, and the soundtrack is composed live while you play.

## How it works

| Piece | File | Role |
|---|---|---|
| Sound bank | `Source/Commanders/Audio/CmdSynth.cpp` | Builds about 40 effects (weapons, explosions, UI, announcer chimes) from noise, oscillators, filters and envelopes. This takes roughly 135 ms at startup. |
| Music engine | `Audio/CmdMusic.cpp` | A real-time 16th-note sequencer playing drums, bass, a detuned pad, an arpeggiated lead with ping-pong delay, bells and drones. It re-rolls its arrangement every 8 bars, so it doesn't repeat exactly. Rendering costs about 0.4% of one CPU core. |
| Mixer | `Audio/CmdAudioMixerComponent.cpp` | A `USynthComponent` that mixes the music and up to 40 effect voices on the audio thread, with resampling, constant-power panning and a peak limiter. It keeps playing while the game is paused. |
| Subsystem | `Audio/CmdAudioSubsystem.cpp` | The game-thread API. It sets volume and pan from the camera's view, rate-limits repeats (for example 40 machine guns firing in one frame), tracks combat intensity, and stores volume settings. |
| Audio actor | `Audio/CmdAudioActor.cpp` | One per level. It picks the music mood from the game flow and plays the victory and defeat stings. |

### Music moods
| Game state | Mood | Character |
|---|---|---|
| Main menu | **Menu** | Slow D minor: pads, a pulsing bass and sparse pentatonic bells |
| Briefing, cutscenes | **Tension** | E minor drone, timpani and an ominous ♭II chord |
| Playing | **Battle** | A minor industrial march at 128 BPM. Combat near the camera raises the intensity: calm play gives a light groove, firefights bring full drums with snare fills, crashes and a lead arpeggio. |
| Victory | **Victory** | C major: I–V–vi–IV with a bright arpeggio and bells |
| Defeat | **Defeat** | Slow minor drone with falling bells |

Mood changes cross-fade: the old mood fades out over about 1.5 s, then the new one fades in.

## Playing sounds from code

```cpp
#include "Audio/CmdAudioSubsystem.h"

UCmdAudioSubsystem::Play2D(this, TEXT("click"));                 // UI / announcer
UCmdAudioSubsystem::PlayAt(this, TEXT("explode_large"), Location); // positional
GameState->AddMessage(TeamId, TEXT("Text"), Color, TEXT("unit_ready")); // announcer line + sound
```

Weapons name their sound in `Content/Data/Weapons.json` (`"sound": "cannon"`). If a weapon has no sound, one is picked from its warhead.

## Sound ids
* **Weapons:** `gun_rifle`, `gun_mg`, `gun_pistol`, `gun_sniper`, `gun_chain`, `cannon`, `cannon_heavy`, `rocket`, `mlrs`, `artillery`, `flame`, `zap`, `flak`, `aurora`
* **Impacts:** `explode_small`, `explode_medium`, `explode_large`, `collapse`, `death`
* **Actions:** `deploy`, `place`
* **UI and announcer:** `click`, `deny`, `select`, `select_vehicle`, `select_building`, `ack`, `ack_attack`, `message`, `queue`, `build_complete`, `unit_ready`, `credits`, `capture`, `objective`, `objective_failed`, `reinforce`, `low_power`, `alarm`, `radio`
* **Stings:** `victory`, `defeat`

`python3 Tools/validate_data.py` checks that every id used in the data files and in the C++ exists in the bank.

## Mixing and replacing with real audio

`Content/Data/Audio.json`:
```jsonc
{
  "gain": { "gun_mg": 0.55 },                                         // per-sound volume
  "sfx_assets": { "explode_large": "/Game/Audio/SFX_BigBoom.SFX_BigBoom" },   // replace one effect
  "music_assets": { "battle": "/Game/Audio/Music/MUS_Battle.MUS_Battle" }    // replace a music mood
}
```
Overridden effects play through `UGameplayStatics::PlaySound2D`. An overridden music mood plays on its own audio component with fades, and the procedural music for that mood is muted.

## Player settings
Music and effects volume can be changed on the main menu and the pause menu, and are saved. Press `M` to mute or unmute the music.
