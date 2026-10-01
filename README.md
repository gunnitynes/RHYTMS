# rhytms

*a rhythmic sostenuto.* It listens, remembers, and plays your sound back as rhythm.

A sostenuto pedal holds only the notes that were already down when you pressed it.
RHYTMS uses the same idea on rhythm. It catches the strikes in whatever you feed it
(a tapped table, a loop, a voice, a field recording), keeps them as a living memory,
and plays them back on three circular rhythms that grow and drift as they turn.
Each struck memory can stay a dry chop or bloom into a held, frozen shimmer.

VST3 / AU / Standalone, built with JUCE 8.

---

## How it thinks

```
input ──► memory (last 20 s) ──► strikes cut out by an onset detector
   │                                      │
   └──► mirror: a one-bar fingerprint      ▼
        of *where* you tend to strike   three orbits choose when to strike and which memory
                                           │
                                           ▼
                           voices: play, catch, hold (bloom)
                                           │
                                     dust ─► halo ─► out
```

* **Memory.** The input is recorded all the time. An onset detector cuts it into
  *strikes*, and the newest `memory` strikes are what the orbits play.
* **Hold (sostenuto).** Freezes the memory. Whatever you just played is kept, and new
  input passes through untouched while the orbits keep playing what was held.
* **Mirror.** While you play, RHYTMS learns where in the bar your hits land (the ochre
  ring around the orbits). The orbits lean toward that fingerprint, each in its own
  way:
  * **i** follows it as played,
  * **ii** plays it backwards,
  * **iii** fills the gaps you leave.

  Feed it a groove and its echo comes back changed.
* **Orbits.** Each orbit is a euclidean circle: `pulses` spread as evenly as possible
  over `steps`, turned by `rotate`, at its own `rate` (including triplets, dotted and
  quintuplets). Different step counts and rates drift against each other into slow
  polyrhythms.
* **Evolve.** Each time an orbit comes full circle, a cellular automaton may rewrite it
  (rule 90 in calm weather, rules 30 and 110 in storms), while a pull back to the
  euclidean skeleton keeps it recognisable. The polygon each orbit draws shows its
  current shape.
* **Bloom.** At zero, each strike plays its slice and stops. Turning it up makes the
  voice catch the sound earlier and hold it with overlapping grains, so a single snare
  hit becomes a sustained, breathing tone with a long tail. This is the sostenuto heart
  of the plugin.

## Shaping it: the weather pad

One gesture shapes everything at once:

| | |
|---|---|
| **left → right** | sparse → dense. More pulses on every orbit, more rolls. |
| **bottom → top** | geometry → weather. Bottom: exact, euclidean, repeating. Top: loose timing, ghost notes, stuttering rolls, random memories, octave and fifth leaps, restless grains. |

Drag slowly while it plays. Low and to the left gives sparse geometric figures. High and
to the right gives a dense, rolling storm made of your own sound.

## Direct touch on the orbits

* **Click a step** to pin it: *always* (circled), then *never* (crossed), then free
  again. Right-click or alt-click clears a pin. Pins win over evolution, weather and
  mirror.
* **Scroll over a circle** to rotate it.
* **regrow** forgets what the orbits have grown into. **throw** rolls new shapes for
  all three. **unpin** clears every pin.

## Dissolve

A performance button for letting go. Click **dissolve** and, over **over**
(¼ to 16 bars), the rhythm comes apart:

* strikes thin out and stop rolling, and their timing loosens
* every memory blooms into a held, frozen tone that drifts slightly out of tune
* the sound darkens, more strikes play backwards, older memories surface
* the halo swells

What's left is a slow cloud made of your rhythm. Click again and it gathers back
into the pattern over the same time. Press and hold the button to dissolve only for
as long as you hold it. Dissolve is a parameter, so you can automate it in the DAW.

## Harmony: the musical mode

Turn on **musical** (section v) when you want rhythmic textures that are also
harmonic: a drum loop that plays chords, or plucks that stay in key.

* **Pitch tracking.** Each caught strike is measured once, just after its attack.
  Pitched sounds (voice, plucks, bass, kalimba) are retuned from the note they
  really are. Unpitched sounds (snare, claps, breath) are heard as the key's root,
  so their transpositions land in key too.
* **key** and **scale:** 14 scales, from major, minor, dorian, phrygian, lydian,
  mixolydian and harmonic minor to pentatonics, whole tone, hirajoshi, pelog and
  in sen.
* **chords** and **every:** a progression locked to the bar (*still, I V vi IV,
  i VI III VII, ii V I, pendulum, fifths, wander*), with each chord lasting ½ to 8
  bars. Chords are built from the chosen scale, so a pelog progression stays pelog.
* **harmony:** how often a strike becomes a tone of the current chord (root, third,
  fifth, sometimes the seventh) rather than just a note of the scale. At 1, every
  strike is a chord tone.
* **snap:** how firmly notes are pulled into place. Below 1, they hang between
  notes, microtonally.
* **ring:** a resonator inside every voice, tuned to the note it plays, so even a
  burst of noise sings at a pitch. Low settings add a tuned shimmer, high settings
  turn every hit into a struck string or bar.
* The orbit **pitch** knobs and weather's octave and fifth leaps still apply. Their
  results are then pulled into the key.

The strip on the right shows the key and the current chord as a roman numeral. Ink
dots mark the scale, red rings mark the chord, and each note flashes when a strike
lands on it. Key and scale start locked, so randomize and morph never change your
key. Unlock them (alt-click) if you want them to.

## Presets, randomize, morph, undo

The header carries the same toolkit as the other plugins:

* **Presets:** a dropdown with *save as...* first, then **factory** and **user**
  sections, plus ‹ › to step through them. A preset is the whole state: every knob,
  the pinned steps and the knob locks. Loading a preset never changes sync, hold,
  morph, dissolve or the output level. Sixteen factory presets come with the plugin:
  *Init, Tapped Gamelan, Ghost Choir, Stutter Poem, Polymeter Clock, Rain on Tin,
  Slow Bells, Broken Machine, Mirror Mirror, Dust Waltz*, and six musical ones:
  *Kalimba Rain, Drum Choir, Modal Bells, Pelog Garden, Glass Progression,
  Dissolving Hymn*.
  Files live in `~/Library/Application Support/gunnitynes/RHYTMS/Presets` on macOS
  and `%APPDATA%\gunnitynes\RHYTMS\Presets` on Windows.
* **randomize** rolls every unlocked knob. The **▾** next to it steers the roll
  towards a kind of rhythm: *pulse, bloom, storm, sparse, harmonic, mirror* (or *any*).
* **Knob locks:** alt-click a knob to lock it (ochre dot). Ctrl/cmd-drag a knob to
  limit where randomize may put it (red arc). Ctrl/cmd-click clears the range, and
  right-click opens a menu. Dry, wet, sense, key and scale start locked, and orbit
  pitch starts limited to an octave either way.
* **morph** lets every unlocked knob drift continuously between random states over
  **time** (0.5–60 s). Locked knobs stay put, so you can lock the parts you love and
  let the rest wander.
* **undo / redo** step through randomize, throw, morph and preset changes.
* The window is **resizable** (drag the corner), remembers its size, and never
  opens taller than your screen.

## Sync

* **sync to daw** (default): tempo, time signature and bar position follow the host.
  The orbits lock to the timeline, so step 1 lands on the bar. When the transport is
  stopped, RHYTMS keeps running at the host tempo so live input still gets played.
* Turn sync off to run free at **free tempo** (30–240 bpm).

## Controls

**i. listen**: `sense` (onset sensitivity), `memory` (how many recent strikes are in
play), `free tempo`, `hold`.

**iii. bloom & space**

| control | what it does |
|---|---|
| `bloom` | from chop to frozen sostenuto |
| `evolve` | how often the orbits rewrite themselves |
| `mirror` | how much the orbits follow your own rhythm |
| `swing` | delays every other step |
| `halo` | a darkening, tempo-locked diffuse echo |
| `dust` | tape wobble and saturation on the struck sound |
| `dry`, `wet`, `out` | mix and output level |

**iv. voices (per orbit)**

| control | what it does |
|---|---|
| `steps`, `pulses`, `rotate`, `rate` | the euclidean shape and speed |
| `pitch` | transposition in semitones, tape-style (it changes speed too) |
| `reach` | 0 always plays the newest strike, 1 reaches back to the oldest in memory |
| `length` | how long each strike stays open, from ¼ step to 4 steps |
| `colour` | darker (low-pass) ← 0 → thinner (high-pass), resonant at the extremes |
| `pan`, `level`, `reverse` | placement, level, and the chance of a strike playing backwards |

## Starting points

* **Tapped table → gamelan:** bloom 0.6, orbit ii pitch +12, orbit iii pitch +19,
  weather pad low and centred, halo 0.4.
* **Drum loop → ghost choir:** hold after one bar, bloom 0.9, length high, colour
  negative, dry 0.
* **Voice → stutter poem:** bloom 0, reach 1, reverse 0.4, weather pad high and to the
  right.
* **Polymeter clock:** steps 16 / 12 / 7, rates 1/16, 1/8T, 1/16Q, evolve 0, weather
  pad at the bottom.

## Installing a downloaded build

Each GitHub Actions run produces one zip per platform, with an `INSTALL.txt` inside.
The zip is wrapped in the artifact's own zip, so unzip twice.

* **macOS:** copy `RHYTMS.vst3` to `~/Library/Audio/Plug-Ins/VST3`, then clear the
  download quarantine, or Live will silently skip the plugin:
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/RHYTMS.vst3`
* **Windows:** copy the whole `RHYTMS.vst3` folder to `C:\Program Files\Common Files\VST3`.
* **Ableton Live:** in Settings › Plug-Ins, turn on *Use VST3 Plug-in System Folders*
  and click Rescan. RHYTMS appears under Plug-Ins › VST3 › **gunnitynes**.

## Building

You need CMake 3.22+ and a C++20 compiler. JUCE 8.0.4 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To build and install in one go, add `-DRHYTMS_INSTALL=ON` to the first command. Each
build then copies the plugins into this machine's plugin folders. On a Mac, quit Live
first, then rescan.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRHYTMS_INSTALL=ON
cmake --build build --config Release
```

Without it, the plugin ends up in `build/RHYTMS_artefacts/Release/VST3/RHYTMS.vst3`. Copy it to:

* `~/Library/Audio/Plug-Ins/VST3` on macOS (the AU goes in `~/Library/Audio/Plug-Ins/Components`)
* `C:\Program Files\Common Files\VST3` on Windows
* `~/.vst3` on Linux

To use a local JUCE checkout instead, add `-DRHYTMS_JUCE_DIR=/path/to/JUCE`.

On Linux, install the JUCE dependencies first:
`libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev`.

Every push also builds macOS (universal), Windows and Linux binaries through GitHub
Actions. Download them from the run's artifacts.

## Source layout

```
Source/
  PluginProcessor.*   clock, DAW sync, scheduling, parameters, state
  PluginEditor.*      layout
  dsp/Capture.h       memory, onset detection, slices, mirror fingerprint
  dsp/Orbit.h         euclidean skeleton + cellular-automaton growth
  dsp/Voice.h         slice playback, freeze grains (bloom), filter, ring resonator
  dsp/Halo.h          dust (wow + saturation) and halo (diffuse echo)
  dsp/Harmony.h       keys, scales, progressions, YIN pitch tracking
  state/PresetManager factory / user preset files
  state/MorphEngine   randomize, morph, knob locks and ranges, categories
  ui/Look.h           paper & ink look, dials
  ui/Views.h          orbit rings, weather pad, memory strip
```
