# The Snark Barker Diagnostic Program
Have you built a Snark Barker but it doesn't work right? Having trouble
figuring out exactly what is wrong? SBDIAG may be able to help!

## Usage
* Download the executable along with TADA.WAV and copy it to your vintage
system.
* Run SBDIAG.EXE
* Hit the number keys to pick a menu option
* Hit escape to go up one menu level or to quit.

## Troubleshooting
Unlike games and other software SBDIAG makes no assumptions about what is or
is not working on the hardware. The following few sections provide some basic
troubleshooting advice, primarily geared towards the Snark Barker, although
SBDIAG can be used with any Sound Blaster compatible sound card. It helps
to have the [schematic](https://github.com/schlae/snark-barker/blob/master/SnarkBarker.pdf) in front of you so you can follow along.

### SB DSP
The DSP is actually an 8051 microcontroller. It communicates with the host PC
using two latches that form a simple mailbox.

The first test on the menu, **DSP Address Test**, allows you to check the
address decode circuitry on the sound card. Run the test, and put a logic
probe or oscilloscope on pin 10 of U8 to confirm that the DSPRD\_CS# signal
is pulsing. You can also check pin 3 of U16 for the CARD\_SELECTED# signal.
Furthermore, check pin 3 of U27 to make sure ISA\_RD# is pulsing.

The **DSP Write Test** just writes the "disable speaker" command
in a loop. You can check pin 11 uf U18 (ISA\_WRITE#) to make sure the control
logic is sending the signal to the mailbox latch. If the DSP is alive, then
pin 1 of U18 (DSP\_READ#) will assert when the DSP tries to read the command.
Check pin 28 of U13 for the DAV\_DSP signal, which lets the DSP know that a
byte is waiting for it in the mailbox.

**DSP Reset** does what it says, pulsing the reset line on the DSP (pin 9 of
U13). If you don't see such a signal, backtrack through the reset logic of
U29, U30, U26, and W24.

**DSP Version** retrieves the two-byte version number of the DSP firmware.
If this returns a number that makes sense (like 2.02) then you know that the
DSP mailbox is working and the DSP is programmed correctly.

**DSP Interrupt** tests the interrupt logic by commanding the DSP to assert
the IREQUEST line (pin 12 of U13). You should see this line pulsing during the
test. You can also check pin 9 of U26 which is the actual interrupt line. The
RD\_DAV# line, is pulsed when the host PC tries to read 2xEh (the data
available flag) which clears the interrupt flag. This is pin 9 of U28.

**Generate Sine Wave (no DMA)** uses a DSP diagnostic command to generate a
sine wave. This test does not require working a working interrupt or DMA. You
 should hear a tone coming out of the speakers. If not, work your way through
the audio chain starting from U4, R4/R3, U5 (pin 1), and U5 (pin 8). The DAC
(U12) is current based so you will not see audio voltages on the output pin.
You can check for pulses on the incoming data lines (pins 5-12 of U12).

**Generate Sine Wave (DMA)** streams a digitally-generated sine wave to the
DSP using DMA and the interrupt. If you can hear this, then the digital section
of the card is working. If you hear it for 1 second and then silence, the
interrupt is not working.

**Play Digital Audio** this plays a familiar sound through the speaker. :-)

### Ad Lib Tests

The Ad Lib section of the card uses the YM3812 and Y3014 chips to play back
FM-synthesized audio. The **Try to Detect Ad Lib** menu option runs the
traditional detection algorithm that uses the YM3812's internal timer to
set a status bit. This is a good way to check U9's CS# (pin 7),
READ# (pin 6), WRITE# (pin 5), and A0 (pin 4) signals on the YM3812. While
you are there, make sure there is a 3.58MHz square wave on pin 24. If you
don't see a signal on one of those pins, work your way back through the logic
that drives those pins.

**Play Sine Wave** uses the YM3812 to generate a basic 1KHz sine wave. If you
don't hear anything but the Ad Lib was detected OK, check the audio path
starting from the digital CLOCK (U10 pin 5), LOAD (U10 pin 3) and SD (U10
pin 4) signals, moving to the audio output (U10 pin 2), buffer (U11 pin 14),
and mixer (U11 pins 8 and 7). The YM3812 registers are written only once, so
this test is not very useful for checking the YM3812's host interface.

**Play Percussion Loop** is meant for checking digital writes to the YM3812
registers. There are no read operations during this test.

### CMS Chip Tests

The CMS chips (optional) provide Game Blaster compatibility. If you have
installed them, this part of SBDIAG can help you test it. Since the chips
are write-only, SBDIAG has no way to confirm any tests; you will have to
check pins using a logic probe or oscilloscope, or listen for the audio
output.

**Write Loop** tests simply write to a CMS register over and over again. You
will not hear any sound but you can probe for activity on the following pins:

* CS# (pin 2)
* WR# (pin 1)
* A0 (pin 3)
* DTACK# (pin 7)

**Sine Wave** tests generate a 1KHz sine wave through the left channel, right
channel, or both channels at once. These tests write to the registers only
twice, once before the test and once afterwards to shut off the sound, so this
test is not very useful for use with a logic probe.

### SB-MIDI Tests
The Snark Barker, just like older Sound Blaster cards, has a very basic SB-MIDI
mode (not MPU-401 compatible). Not a whole lot of software uses it and the
wiring is incredibly simple (basically just 8051 UART lines wired to the
joystick port). The tests are very simple and just test the port output and
input capabilities. Note that the loopback test will occasionally return
bad values even when it is working OK.

### Joystick Tests
The PC joystick port is very basic and uses a quad timer chip along with a
simple resistor-capacitor circuit to measure the resistance of the joystick
potentiometers.

**Get raw port status** just returns the raw bits that it reads from the
joystick port. If you have a socketed NE558, you can remove it from the socket
and manually jumper the U3 inputs (pins 2, 4, 6, 8, 11, 13, 15, and 17) to
ground in order to verify that the U3 buffer is working and that the address
hardware also works. You can check U3 pin 1 to verify that the address
decoder is working. Also, joystick buttons can be tested in this state.

**Measure joystick potentiometers** performs the standard PC joystick algorithm
and returns the raw counter values for each axis. You can check that the
trigger signal is correctly appearing at the NE558 by checking pin 3 of U1.

## Building
If you want to build SBDIAG, use Borland Turbo C++ version 3.1 under DOS. Be
sure to set the memory model to "large" or it will not be able to play back
digital audio. This program will run in DOSBOX or on a real vintage computer.

## License
This work is licensed under a Creative Commons Attribution-ShareAlike 4.0
International License. See [https://creativecommons.org/licenses/by-sa/4.0/](https://creativecommons.org/licenses/by-sa/4.0/).
