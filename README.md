<h1 align="center"><!-- HTML4, but GitHub strips inline styles -->
  timesignal
  <br>
  <br>
  <img src="docs/timesignal.svg" alt="Logo" width="128" height="128">
  <br>
  <br>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3">
  </a>
</h1>


## Overview

**timesignal** turns a Linux computer with suitable audio output hardware into
a low-frequency radio transmitter broadcasting a time signal that can
synchronize most radio-controlled (&ldquo;atomic&rdquo;) clocks and watches.

Real time signal broadcasts are limited in geographic range and notoriously
prone to interference in urban areas, so many such clocks end up never actually
using their self-setting functionality. **timesignal** may allow setting such
clocks when/where a suitable signal is not otherwise available.

## Features

- **Compatible with most radio-controlled clocks**: Emulates the five
  operational radio time signal stations
  ([&#127464;&#127475; BPC](https://en.wikipedia.org/wiki/BPC_(time_signal)),
  [&#127465;&#127466; DCF77](https://en.wikipedia.org/wiki/DCF77),
  [&#127471;&#127477; JJY](https://en.wikipedia.org/wiki/JJY),
  [&#127468;&#127463; MSF](https://en.wikipedia.org/wiki/Time_from_NPL_(MSF)),
  and [&#127482;&#127480; WWVB](https://en.wikipedia.org/wiki/WWVB)).

- **Custom time/timezone support**: Transmits the current time or a custom time
  configured by the user.

- **BST/CEST/DST-aware**: Transmits daylight saving time information for DCF77,
  MSF, and WWVB.

- **Leap second-aware**: Transmits a DUT1 offset for MSF and WWVB.

## Installation

Standalone binaries and `.deb` packages are available on the
[Releases](https://github.com/kangtastic/timesignal/releases) page.

Download the correct binary or package for the CPU architecture of the computer
on which **timesignal** will be installed:

| Filename contains| Architecture | Examples (not exhaustive) |
| - | - | - |
| `amd64` | x86-64 | Most ordinary desktops, laptops, mini PCs, and servers |
| `arm64` | AArch64 | Single-board computers (SBCs) based on ARMv8/ARMv9 CPUs<br>Raspberry Pi devices running any <u>**64-bit**</u> OS |
| `armhf` | Newer AArch32 | Older SBCs based on ARMv7 CPUs<br>Raspberry Pi devices running a <u>**32-bit**</u> OS that is <u>**not**</u> Raspberry Pi OS |
| `armhf-rpi` | Older AArch32 | Even older SBCs based on ARMv6 CPUs with VFP2<br>Raspberry Pi devices running <u>**32-bit**</u> Raspberry Pi OS |
| `riscv64` | RISC-V 64-bit | SBCs based on RISC-V CPUs<br>StarFive VisionFive 2

If the provided binaries/packages are not suitable, see
[Building from source](#building-from-source).

### Standalone binaries

A standalone binary does not require formal installation, but it must have
execute permissions added to it at the filesystem level before it can be run.

For an `amd64` binary named `timesignal-amd64`, this may be accomplished with:

```sh
chmod +x timesignal-amd64
```

The binary may then be run with:

```sh
./timesignal-amd64
```

### Packages

For an `amd64` package named `timesignal_0.2.0-1_amd64.deb`:

```sh
sudo apt install timesignal_0.2.0-1_amd64.deb
```

Many systems also support installing `.deb` packages via a graphical installer
of some sort, perhaps by simply opening the package file in a file manager. The
exact method varies between systems and is beyond the scope of this document.

After installation from a package, **timesignal** may be run with:

```sh
timesignal
```


## Usage

<img src="docs/timesignal.gif" alt="Usage demonstration">

```sh
timesignal [OPTION]... [STATION]
```

### Quick option reference

<details>
<summary>click to expand/hide</summary>

#### Time station selection

|   | Description | Allowed values | Default value |
| - | ----------- | -------------- | ------------- |
| `STATION` | time station to emulate | `BPC`, `DCF77`, `JJY`, `JJY60`, `MSF`, `WWVB` | `WWVB` |

Note that `STATION` is uniquely a positional argument.

#### Time signal options
| Option | Description | Allowed values | Default value |
| ------ | ----------- | -------------- | ------------- |
| **-b**, **--base**=`BASE` | time base in `YYYY-MM-DD HH:mm:ss[(+-)hhmm]` format | `1970-01-01 00:00:00+0000` to `9999-12-31 23:59:59 +2359` | current system time |
| **-o**, **--offset**=`OFFSET` | user offset in `[+-]HH:mm:ss[.SSS]` format | `-23:59:59.999` to `+23:59:59.999` | `00:00:00.000` |
| **-d**, **--dut1**=`DUT1` | DUT1 value in ms (only for MSF and WWVB) | `-999` to `+999` | `0` |

#### Timeout options

| Option | Description | Allowed values | Default value |
| ------ | ----------- | -------------- | ------------- |
| **-t**, **--timeout**=`TIMEOUT` | time to run before exiting in `HH:mm:ss` format | `00:00:01` to `23:59:59` | forever |

#### Sound options (rarely needed)

| Option | Description | Allowed values | Default value |
| ------ | ----------- | -------------- | ------------- |
| **-m**, **--method**=`METHOD` | output method | `pipewire`, `pulse`, `alsa` | autodetect |
| **-D**, **--device**=`DEVICE` | output device (only for ALSA) | ALSA device name | `default` |
| **-f**, **--format**=`FORMAT` | output sample format | `S16`, `S16_LE`, `S16_BE`,<br>`S24`, `S24_LE`, `S24_BE`,<br>`S32`, `S32_LE`, `S32_BE`,<br>`U16`, `U16_LE`, `U16_BE`,<br>`U24`, `U24_LE`, `U24_BE`,<br>`U32`, `U32_LE`, `U32_BE`,<br>`FLOAT`, `FLOAT_LE`, `FLOAT_BE`,<br>`FLOAT64`, `FLOAT64_LE`, `FLOAT64_BE` | `S16` |
| **-r**, **--rate**=`RATE` | output sample rate | `44100`, `48000`, `88200`, `96000`,<br>`176400`, `192000`, `352800`, `384000` | `48000` |
| **-c**, **--channels**=`CHANNELS` | output channels | `1` to `1023` | `1` |
| **-S**, **--smooth** | smooth rapid gain changes in output waveform | provide to turn on | off |
| **-u**, **--ultrasound** | enable ultrasound output<br>(**MAY DAMAGE EQUIPMENT**) | provide to turn on | off |
| **-a**, **--audible** | make output waveform audible<br>(for entertainment only) | provide to turn on | off |

#### Configuration file options

| Option | Description | Allowed values | Default value |
| ------ | ----------- | -------------- | ------------- |
| **-C**, **--config**=`CONFIG_FILE` | load options from a file | filesystem path | none |

See [**timesignal.conf**(5)](https://kangtastic.github.io/timesignal/timesignal.conf.5.html)
for more information on the configuration file format.

#### Logging options

| Option | Description | Allowed values | Default value |
| ------ | ----------- | -------------- | ------------- |
| **-l**, **--log**=`LOG_FILE` | log messages to a file | filesystem path | none |
| **-L**, **--syslog** | log messages to syslog | provide to turn on | off |
| **-v**, **--verbose** | increase logging verbosity | provide to turn on | off |
| **-q**, **--quiet** | suppress logging to console (and only console) | provide to turn on | off |

#### Miscellaneous

| Option | Description |
| ------ | ----------- |
| **-h**, **--help** | show a help string and exit |
| **-H**, **--longhelp** | also show allowed and default option values |

</details>

> [!NOTE]
> Consider providing [**-S**/**--smooth**](#sound-options-rarely-needed) if
> **timesignal** will be used for an extended period of time.
>
> It may cause some clocks to fail to synchronize, but it also prevents any
> popping/clipping in the output that could eventually damage audio equipment.

See the [man pages](#man-pages) for more information on options and the
configuration file format.

### Instructions

1. Turn down the volume.
2. Start **timesignal** with the desired options.
3. Force a synchronization attempt on the receiving radio-controlled clock.
4. Position the clock near the speaker, e.g. resting on top of it.
5. Slowly turn up the volume until either:
   - the clock picks up the time signal with good reception, or
   - the volume would be too loud if the speaker was playing ordinary music.
6. Depending on what happened in Step 5:
   - if the clock picked up the time signal, wait for up to three full minutes
     for synchronization to complete, or
   - repeat the process with small variations in clock positioning
     (rotate/flip/move the clock) to try to improve reception.

> [!WARNING]
> When using **timesignal** with speakers,
> **DO NOT PLACE YOUR EARS NEAR THE SPEAKERS**.
>
> The generated audio has full dynamic range, but is pitched high enough to
> be difficult to perceive.
>
> **Even if you can&rsquo;t hear anything**, some equipment is capable of
> playing it back loud enough to potentially cause
> **PERMANENT HEARING DAMAGE**.


### Man pages

HTML versions of **timesignal**&rsquo;s man page documentation are provided
below.

[**timesignal**(1)](https://kangtastic.github.io/timesignal/timesignal.1.html):
Includes explanations of option effects and examples.

[**timesignal.conf**(5)](https://kangtastic.github.io/timesignal/timesignal.conf.5.html):
Information about the configuration file format.


## Technical details
<details>
<summary>click to expand/hide</summary>
<p><b>timesignal</b> generates an audio waveform intentionally crafted to
create, when played back through consumer-grade audio hardware, the right kind
of radio frequency (RF) noise to be mistaken for a time signal broadcast.</p>

<p>Specifically, given a fundamental carrier frequency used by a real time
station, it generates and modulates the highest odd-numbered subharmonic that
also falls below the Nyquist frequencies of common playback sample rates.</p>

<p>One of the higher-frequency harmonics inevitably created by any real-world
DAC during playback will then be the original fundamental, which should leak to
the environment as a short-range radio transmission via the ad-hoc antenna
formed by the physical wires and circuit traces in the audio output path.</p>
</details>


## Building from source

<details>
<summary>click to expand/hide</summary>

All commands/paths below should be run in/are relative to
**timesignal**&rsquo;s source root directory.

### Main program and man page documentation

At least one of the following development libraries is required according
to the audio output method(s) that **timesignal** should support. Having all
of them is recommended if feasible. Package names for popular Linux
distributions are provided for reference.

| Prerequisite | Debian/Ubuntu | Fedora/RHEL | Arch |
| ------------ | ------------- | ----------- | ---- |
| `libpipewire` | `libpipewire-0.3-dev` | `pipewire-devel` | `libpipewire` |
| `libpulse` | `libpulse-dev` | `pulseaudio-libs-devel` | `libpulse` |
| `alsa-lib` | `libasound2-dev` | `alsa-lib-devel` | `alsa-lib` |

Building, installing, and uninstalling **timesignal** and its man page
documentation may then be accomplished as follows:

```sh
make
sudo make install
sudo make uninstall
```

### Tests

CMake must be installed and [cmocka](https://cmocka.org) (1.1.x) must be
present in source code form in the `tests/cmocka` subdirectory.

Populating `tests/cmocka` may be accomplished by downloading and extracting
[a tarball](https://cmocka.org/files/1.1), or by simply running:

```sh
git submodule init --recursive
```

Building and running tests may then be accomplished by running:

```sh
make run-tests
```
</details>

## Feasibly asked questions (FAQ)

- **What does &ldquo;suitable audio output hardware&rdquo; mean?**
  <br>
  Unfortunately, there isn&rsquo;t really a way to tell ahead of time. As
  outlined in [Technical details](#technical-details), **timestation** works
  by exploiting normally inconsequential imperfections in audio hardware,
  and plenty of hardware (especially the audiophile-grade stuff)
  &ldquo;helpfully&rdquo; filters out the effects of such imperfections.
  The only way to know for sure if a given setup works is to try it.

- **What are some examples of hardware that does and doesn&rsquo;t work?**
  <br>
  Keep in mind that these examples are anecdotal.

  - **Tested working**: Onboard audio with generic computer speakers, HDMI
    output to an A/V receiver with home theater speakers, some USB speakers.

  - **Tested _not_ working**: Some other USB speakers, HDMI output to a
    monitor with built-in speakers, Bluetooth speakers/headphones/DACs
    (unsurprising due to A2DP audio compression).

- **Would an antenna designed to plug into a 3.5mm jack work?**
  <br>
  Probably. Such antennas have worked in the past with programs similar to
  **timesignal**. How to construct one is beyond the scope of this FAQ.

- **Why so many sound options if they&rsquo;re &ldquo;rarely needed&rdquo;?**
  <br>
  ALSA. :sweat_smile:

- **Can enabling the -u/--ultrasound option really damage equipment?**
  <br>
  Probably not &mdash; it _shouldn&rsquo;t_ &mdash; but it _could_. Speakers
  don&rsquo;t like high frequencies and high power over an extended period of
  time. As for the other links in the audio output chain, all bets are off if
  electronic components are ever used &ldquo;outside design parameters&rdquo;.
  In short, enabling ultrasound output can only ever increase the likelihood
  of damage (assuming the equipment does play it back correctly).

- **I&rsquo;d like to hear what timesignal is doing.**
  <br>
  Pass **-a**/**--audible** or set `audible=On` in `/etc/timesignal.conf`.

- **I don&rsquo;t have Linux/I don&rsquo;t want to install anything. Can I
  still use timesignal?**
  <br>
  [More or less.](https://github.com/kangtastic/timestation)


## License

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
