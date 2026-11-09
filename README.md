# Dehli Musikk audio plugins

Two plugins, each one of the output machines from [Overtonium](https://github.com/benjamindehli/overtonium) on its own, for any track or bus:

- **Console**: a console's mix bus, measured from a real chain of a transformer, a bus compressor and a tape stage. The lows saturate first, the compressor holds the level with an automatic release, and the tape stage adds a steady third harmonic once it is driven.
- **Cassette**: a four-track cassette recorder at 9.5 cm/s with Dolby B on a normal tape, measured from a real deck. The lows saturate first, the top rolls off, the speed wanders with wow and flutter, and the hiss follows the music.

Both end in a limiter with its ceiling at -0.1 dBFS, so neither goes past full scale however hard it is driven. They are the same code Overtonium runs, from [dehli-musikk-plugin-core](https://github.com/benjamindehli/dehli-musikk-plugin-core), whose [notes on the output stage](https://github.com/benjamindehli/dehli-musikk-plugin-core/blob/main/docs/output-stage.md) say how each machine was measured and fitted.

## The controls

- **Input**, -24 to +24 dB: how hard the input goes into the machine. Both machines are at unity for a quiet signal, so the input level is what decides how much they do.
- **Output**, -24 to +24 dB: the level after the machine.
- **Auto gain**: turns the output down by exactly as much as the input turns it up, so a quiet signal stays at its level and only what the machine does to a louder one is heard. Once the machine reaches its ceiling it cannot get any louder, so a loud signal pushed hard comes out quieter than it went in.
- **Delta**: plays only what the plugin adds or takes away, the output less the input, lined up in time. The output level is part of it, and so is the input level when auto gain is off. For Cassette it includes the deck's wow and flutter.

The limiter always looks 2 ms ahead, which lets its gain move smoothly. The plugin reports that as latency, 96 samples at 48 kHz.

The window can be made up to twice its size. Double-click a knob to put it back at 0 dB.

## Formats

VST3 and a standalone application on every platform, Audio Unit on macOS and LV2 on Linux. Mono and stereo.

## Building

You need CMake 3.22 or newer and a C++17 compiler. JUCE and dehli-musikk-plugin-core are downloaded at configure time, each as the archive of one commit, checked against the SHA-256 in `CMakeLists.txt`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The build copies the plugins into your user plugin folders. Pass `-DDM_INSTALL_AFTER_BUILD=OFF` to skip that. Pass `-DJUCE_PATH=/path/to/JUCE` to use a local JUCE, and `-DFETCHCONTENT_SOURCE_DIR_DM_PLUGIN_CORE=../dehli-musikk-plugin-core` to change the library and the plugins together.

On Linux, JUCE needs these headers first:

```sh
sudo apt-get install libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
  libxcomposite-dev libgl-dev libxi-dev libegl-dev
```

Warnings are errors by default. Configure with `-DDM_WARNINGS_AS_ERRORS=OFF` if a compiler this project has not met stops on one.

## Tests

`dm_plugins_test` runs both machines with no host and no display. It checks the parameters, the latency, what the input, output, auto gain and delta do to a tone, that the input at full into a full-scale tone stays within full scale, the bus layouts, the saved state, bypass, a host that sends bigger blocks than it promised, and the window's controls. `dm_plugins_test --render <folder>` saves both windows as PNG files at 1x and 2x instead.

CI builds and tests on Linux, macOS and Windows, runs pluginval at strictness 8 on both plugins, runs the test under the address and undefined behaviour sanitizers, and checks the formatting. The C++ is formatted with clang-format 23.1.1 in the stock LLVM style, and the readme and the workflows with the prettier pinned in `package-lock.json`.

## Licence

GNU Affero General Public License, version 3. See [LICENSE](LICENSE).
