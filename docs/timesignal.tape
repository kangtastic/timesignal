# SPDX-License-Identifier: GPL-3.0-or-later
#
# timesignal.tape: Tape file for VHS terminal GIF utility.
#
# This file is part of timesignal.
#
# Copyright © 2025 James Seo <james@equiv.tech>

Output timesignal.gif

Require echo

Set FontSize 18
Set Width 960
Set Height 512
Set Theme "Cobalt2"
Set Framerate 10
Set TypingSpeed 100ms

Sleep 1s

Type "timesignal -v"
Sleep 1s
Enter

Sleep 24s

Ctrl+C

Sleep 1500ms
