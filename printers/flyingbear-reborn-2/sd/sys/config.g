; FlyingBear Reborn 2 / Nano6. Experimental baseline, not hardware calibrated.
; Drivers are standalone at 32 microsteps; motor current is set on the drivers.
M550 P"FlyingBear Reborn 2"
M669 K1
M569 P0 S1
M569 P1 S1
M569 P2 S1
M569 P3 S1
M569 P4 S1
M584 X0 Y1 Z2:3 E4
M350 X32 Y32 Z32 E32 I0
M92 X160 Y160 Z800 E420
M208 X0 Y0 Z0 S1
M208 X325 Y325 Z350 S0
M566 X300 Y300 Z30 E60
M203 X12000 Y12000 Z600 E1200
M201 X1000 Y1000 Z50 E250
M564 H1
M574 X1 S1 P"!xstop"
M574 Y1 S1 P"!ystop"
M574 Z1 S1 P"!zstop+!z2stop"
M308 S0 P"e0temp" Y"pt1000" R4700
M308 S1 P"bedtemp" Y"thermistor" T100000 B4092 R4700
M950 H0 C"bed" T1
M950 H1 C"e0heat" T0
; Initial bang-bang control. Tune each heater before normal printing.
M307 H0 B1
M307 H1 B1
M143 H0 S110
M143 H1 S300
M140 H0
M950 F0 C"fan" Q250
M950 F1 C"hotendfan" Q250
M106 P0 S0
M106 P1 S1 H1 T50
M563 P0 D0 H1 F0
G10 P0 X0 Y0 Z0 S0 R0
M302 P0
M18
