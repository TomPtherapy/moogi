# Acid Moogi: Quick Reference

Acid Moogi turns the M-VAVE FM-1 into a mono acid box: the four knobs are the filter, the white keys are the 16 steps, EDIT runs the acid generator, and SEQ twice opens the drum machine. Every feature and key combo, by area.

## Play, sound and pages

| Do this | Get this |
| --- | --- |
| PLAY tap | Start / stop |
| PLAY held + SELECT | Tempo (slow turn 1 BPM, fast turn jumps). PLAY blinks in time |
| HOME | The filter screen (also where it powers on) |
| Knobs 1 to 4 | CUTOFF, EMPHASIS, CONTOUR, KBD CTRL. Slow turn = fine steps, fast turn = big moves |
| LFO | CTRL page |
| LFO held (0.5 s) | MONO / POLY (mono = last note wins, slides; poly = 12 voices, no slide) |
| SCL | Oscillators and mixer |
| ENV | Filter and loudness envelopes |
| FX | Effects |
| SELECT / ALGORITHM / PRESETS | Move cursor / change value / change preset |
| ARP tap / held | Arpeggiator on / off; HOLD on / off |
| GLO | Global menu (GLO again = back one level) |

## Sequencer

One sequence of four 16-step parts (8 to 64 steps). The white keys are the steps: playing step lit, steps with notes glowing.

| Do this | Get this |
| --- | --- |
| REC while playing | Live record into the playing step (REC acts on release) |
| REC while stopped | Step record: each note fills a step and moves on |
| ARP in step record | A rest |
| New note while the last is held | Slide |
| SEQ held + white key | Move the cursor to that step |
| SEQ held + black 1 to 4 | Show that part; double press = part on / off |
| SEQ held + black 5 | HALF (last part plays 8 steps) |
| SEQ held + REC | Clear the sequence (notes and knob motion) |
| GLO > KNOB REC | Record knob moves with the notes (off by default) |
| GLO > USB AUDIO | What the computer records over USB: MIX (stereo, as before) or 10 CH (below) |
| REC held + ARP | Undo knob motion since the last REC; notes stay |

## Acid generator

Writes a 16-step acid line into the sequence. Same seed = same line. Accents get louder and open the filter, like a 303.

| Do this | Get this |
| --- | --- |
| EDIT twice | Generator on (EDIT lit) / off |
| EDIT tap | New line from the next loop |
| EDIT held + key | Root note |
| EDIT held + SELECT | DENSITY (how many steps play) |
| EDIT held + ALGORITHM | MOVEMENT (jumps to the high voice) |
| EDIT held + PRESETS | TENSION (an outside note that resolves to the root) |
| EDIT held + knob 1 / 2 | ACCENTS / SLIDES |
| EDIT held + knob 3 / 4 | NOTES in the pool (1 to 5) / RANGE (1 or 2 octaves) |
| No EDIT held | Knobs and wheels do their normal jobs |

**GLO > ACID:** SCALE (PHRYG, PH DOM, MINOR, HARM, PENTA, DORIAN, LOCRI, ACID), ROOT, NOTE LENGTH, STEP, HIGH RANGE, VELOCITY, ACCENT VEL, BREATHE (nudges steps every few loops) with DEPTH and BACK, 4 LFOs, HISTORY (last 10), FAVORITES (7, kept at power-off), SAVE FAVORITE. While the generator is on, REC does nothing.

## Drum mode

Eight sounds (KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM, BELL) on a euclidean beat of 16 steps: HITS spreads the hits evenly, ROTATE shifts them. The drums keep playing outside drum mode. Rule: what you hold is what you edit.

| Do this | Get this |
| --- | --- |
| SEQ twice | Drum mode in / out (HOME or a page button also leaves) |
| Black 1 to 8 tap | Mute / unmute on the next quarter (several at once = same quarter) |
| Black 1 to 8 held (0.3 s) | Select that sound |
| SELECT / ALGORITHM / PRESETS | HITS / ROTATE / VOLUME of the selected sound |
| White key held + SELECT / ALGORITHM | HITS / ROTATE of that key's quarter only. HITS below 0 = "=" (follow the track) |
| Black key held + PRESETS | CHANCE of that sound (0 to 100 %) |
| Knobs 1 to 4 | Still the bass filter |
| SEQ held | Bass sequencer layer, as usual |

Lights: white keys = selected sound's hits (dim) and the playing step (bright). Black keys = lit heard, off muted, blinking waiting for the quarter.

**GLO > DRUMS:** KIT (STD, 80, 10, 66, 55, 77, DEEP, TIGHT, PERC), SIDECHAIN (OFF, 5 to 100 %: each kick pumps the bass and its echoes), RELEASE (1/16 to 1/2, default 1/4: back just as the next kick lands).

## Clock, delay and saving

- **USB clock (norns or any):** followed automatically; the tempo screen shows USB. PLAY waits for the next bar (blinks fast). START from norns only marks the bar; STOP does nothing. Half a second without clock = back to the FM-1's tempo. Send MIDI START from your script, or the bar may land off.
- **Delay TIME** is a note value that follows the tempo: 1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/4D, 1/2. One knob click = one value. Max 1.486 s.
- **USB AUDIO 10 CH** (GLO > USB AUDIO): the FM-1 becomes a 10-input sound card, 16 bits at 44.1 kHz only (set the recording project to 44.1). Inputs 1-2: the bass in stereo with its delay, reverb and chorus (and the pump). Inputs 3-10: the drums dry, one sound each, mono: KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM, BELL, with their VOLUME. All ten sit 12 dB below the FM-1's full level, in the FM-1's own balance (faders at 0 dB = its mix), with a soft top instead of a hard clip: no gain needed in the DAW. The volume knob does not change them. GLO > ABOUT shows, since recording started, USB DROPS (EMPTY: the FM-1 fell behind under load; LATE: a packet not ready in time; should stay 0) and the load: CPU (average) and MAX (the busiest moment), in %. In 10 CH the FM-1 holds about 6 ms of sound in hand before it goes out (MIX: about 3 ms). Changing the setting drops the USB for about a second (MIDI too) while the computer finds the new card. MIX (the default) is the stereo master as before.
- **GLO menu:** MIDI IN, CHANNEL, CC, PITCH BEND, KNOB REC, ACID, DRUMS, USB AUDIO, ABOUT.
- **Kept at power-off:** sequence, acid settings and line, favorites, KNOB REC, drum beat, quarters, VOLUME, CHANCE, kit, sidechain, USB AUDIO. **Not kept:** acid history, drum mutes.

Not yet: drum FILL, BREAK, DROP. Acid Moogi v1.8.0-m8, built on FMini-1 by Ruppert and Felucca by Leo Kuroshita.
