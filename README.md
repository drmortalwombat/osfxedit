# osfxedit
Oscar64 sound FX editor

Click screenshot to download short demo video:

[![Demo Video](https://raw.githubusercontent.com/oschonrock/osfxedit/refs/heads/main/demo.png)](https://github.com/oschonrock/osfxedit/raw/refs/heads/main/demo.mp4)

Keys / functions:
- cursor keys to navigate
- `space` to test the sound effect
- `+` `-` `.` `,` to increase and or decrease a value
- enter filename betwen `[` and `]` hit `return` and select action
- `D09` change drive number

Columns

- 0-E: effect number. Hit `+` here to clone/insert a line (or add a new one when on `#`). Hit `-` to delet this line.
- TSRNG: Enable one of more Waveforms - *T*riangle, *S*awtooth, *R*ectangle, *N*oise. *G*ate determined whether to trigger the gate to "on" at start of this effect. 
- FREQ: Frequency
- PWM: Pulse width - only relevant for Rectangle waveform
- ADSR: Volume Envelope Control: Attack time, Decay time, Sustain Volume, Release time 
- DFREQ: FREQ is changed by this amount every "tick"  (red amounts are negative)
- DPWM: PWM is changed by this amount every "tick"  (red amounts are negative)
- T1: Time the "gate" is on for, in "ticks"
- T0: Time the "gate" is off for, in "ticks"

Ticks

- default is 50Hz PAL / 60Hz NTSC
- can be customised if you compile with `-DOSFXEDIT_USE_NMI -DOSFXEDIT_NMI_CYCLES=8189` to set a tick rate of 8189 clock cycles
