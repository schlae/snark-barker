# Snark Barker 2.02 DSP Firmware Specification

This document is a work in progress. It is an attempt to describe in detail the
operation of the Sound Blaster DSP 2.02 firmware. It's not meant to introduce
you to how Sound Blaster cards play back and record audio, but to describe
how the DSP 2.02 firmware itself works, including previously unknown and
undocumented commands.

Some of the previously undocumented commands and features include:
* The RAM playback command, which loads up to 64 samples into RAM and plays
them back over the speaker.
* The RAM test command, which runs a very basic RAM test on the 8051's
internal SRAM.
* The ROM checksum command, which adds up the contents of ROM into a 16-bit
checksum and returns it (with the two bytes swapped!) to the host PC.

# Pin Assignment Table
| Pin | GPIO | Net       | Dir | Description       |
| --- | ---- | --------- | --- | ----------------- |
| 1   | P1.0 | DAC LSB   | O   | LSB going to DAC output 
| 2   | P1.1 | DAC       | O   |
| 3   | P1.2 | DAC       | O   |
| 4   | P1.3 | DAC       | O   |
| 5   | P1.4 | DAC       | O   |
| 6   | P1.5 | DAC       | O   |
| 7   | P1.6 | DAC       | O   |
| 8   | P1.7 | DAC MSB   | O   | MSB going to DAC output
| --- | ---- | --------- | --- |
| 10  | P3.0 | MIDI RX   | I   | MIDI receive data in
| 11  | P3.1 | MIDI TX   | O   | MIDI transmit data out
| 12  | P3.2 | IREQUEST\# | O   | Pulse low to turn on IRQ to PC
| 13  | P3.3 | DSP\_BUSY | O   | Set high to tell PC we are busy
| 14  | P3.4 | DMA\_EN\#  | O   | Set low to enable DMA requests
| 15  | P3.5 | DREQUEST  | O   | Pulse high to ask PC DMA for more data
| 16  | P3.6 | DSP\_WRITE\# | O | (8051 auto sets this to send data to PC)
| 17  | P3.7 | DSP\_READ\#| O   | (8051 auto sets this to get data from PC)
| --- | ---- | --------- | --- |
| 21  | P2.0 | MUTE\_EN  | O   | Bring high to mute speaker output
| 22  | P2.1 | N/C       |     |
| 23  | P2.2 | N/C       |     |
| 24  | P2.3 | N/C       |     |
| 25  | P2.4 | N/C       |     |
| 26  | P2.5 | MIC\_COMP | I   | Status of mic input: 1=mic \< DAC, 0=mic \> DAC
| 27  | P2.6 | DAV\_PC   | I   | 1=Data waiting in buffer for PC host to read it
| 28  | P2.7 | DAV\_DSP  | I   | 1=Data waiting in buffer for 8051 to read it

## Detailed Pin Descriptions

### IREQUEST
Initial state: high.

Setting this pin low and then high will trigger an interrupt to be sent
to the host PC.

### DREQUEST
Initial state: low.

Set this pin high and then low to trigger a DMA request to be sent to
the host PC.

### DSP\_BUSY
Initial state: high.
Set this pin low when the firmware is ready to receive commands or command
arguments from the host. This pin should be set high while in interrupt
handlers and cleared at the end of the interrupt handler. It should also
go high when a command is received, at least until the command is successfully
latched into the command holding register.

Here's an example sequence.

1. START: set DSP\_BUSY high
2. Preparation, initialization
3. Ready to receive commands. Set DSP\_BUSY low.
4. Command received from host PC. Set DSP\_BUSY high.
5. Parse command byte, dispatch to correct command handler.
6. Command handler requires an argument byte. Set DSP\_BUSY low.
7. Argument byte received from host PC. Set DSP\_BUSY high.
8. Execute command.
9. Done. Go back to waiting for new commands. Set DSP\_BUSY low.

### DMA\_EN\#
Initial state: high.
Pull this pin low to unmask DMA channel requests. This allows the PC's
8237 to transfer data to or from the sound card.

Typically this pin is pulled low when the firmware is ready transfer data
to the host PC. Set the pin high when the entire transfer is completed--i.e.
you run out of samples to play back.

### DSP\_WRITE\# and DSP\_READ\#
These pins are used by the 80C51's external memory access circuitry to
transfer data during a MOVX instruction. They do not need to be touched
at all by firmware.

### MUTE\_EN
Initial state: low

Assert this line high to prevent the DAC output voltage from reaching the
speaker amplifier.

Note that muting and unmuting the DAC output requires a process to prevent
audible pops. See the speaker mute and unmute commands. This line is controlled
only by those two routines (and the start routine that sets it low on boot). 

### MIC\_COMP
This is an input from the microphone input comparator. It outputs a 1 when
the input sample is less than the current DAC value. It outputs a 0 when the
input sample is greater than the current DAC value. It's checked with the
special ADC successive approximation sampling routine.

### DAV\_PC
This pin is high when data is sitting in the output register but the host PC
has not yet picked it up. The pin goes low when the PC reads the data.

By writing to the external output register using movx, this pin automatically
gets set by the hardware.

### DAV\_DSP
This pin goes high when there is data available in the input register from the
host PC. Otherwise, it remains low until the host PC writes to it.

When reading from the external input register using movx, this pin
automatically gets cleared by the hardware.

# List of DSP Commands

Commands are divided into logical groups.

| Upper Nibble | Command Group  |
| ------------ | -------------- |
| 0            | Status         |
| 1            | Playback       |
| 2            | Recording      |
| 3            | MIDI           |
| 4            | Transfer setup |
| 5            | Playback (RAM) |
| 6            | Reserved       |
| 7            | Playback 2     |
| 8            | Silence        |
| 9            | Playback (High Speed) |
| A            | Reserved       |
| B            | Reserved       |
| C            | Reserved       |
| D            | Miscellaneous  |
| E            | Version        |
| F            | Test           |

Each command group checks individual bits in the lower nibble. The checks
often have a priority; entries at the top of the table get executed if the bit
is set, and entries with lower priorities get ignored even if their bits are
set.

Arguments listed for particular commands are single byte only. Often values
are split into high and low bytes.

## Status Group (0x0)
| Lower Nibble Bit | Command | Description
| ---------------- | ------- | ------------
| 3                | Halt    | Enters an infinite loop.
| 2                | Status  | Sends status flags to the host PC. If DMA\_ENABLE\# is low, then the command reenables the timer interrupt.

## Playback Group (0x1)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | Autoinit DMA DAC | None | Starts auto-init DMA playback
| 2                | DMA DAC | Length lo, length hi |Starts regular DMA playback of "length" samples.
| All others       | Direct DAC | byte | Immediately loads DAC with provided sample byte

The above DMA commands check additional bits of the command byte as follows:

| Lower Nibble Bit | Description
| ---------------- | -------------
| 1                | 1=Use 2-bit ADPCM compression, 0=Normal 8-bit uncompressed samples
| 0                | Read additional argument byte, use as starting reference for ADPCM

## Recording Group (0x2)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | Autoinit DMA ADC | None | Starts auto-init DMA recording
| 2                | DMA ADC | Length lo, Length hi | Starts regular DMA recording
| All others       | Direct ADC | None | Immediately reads a sample from the microphone input and returns it as a byte

## MIDI Group (0x3)

| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | MIDI Write Poll | MIDI byte | Sends provided byte out over MIDI port
| 2                | MIDI UART mode | none | Ignores DSP commands until a DSP RESET. All subsequent data is piped through MIDI in both directions.
| All others       | MIDI Read Poll | none | Receives data from MIDI port, returns the raw bytes. End command by sending any byte to the DSP.


The above MIDI commands check additional bits of the command byte as follows:

| Lower Nibble Bit | Description
| ---------------- | -------------
| 1                | Timestamp mode. Adds a 24-bit timestamp to the start of the read data that records the time passed since the command started.
| 0                | Interrupt mode. Generates an interrupt if MIDI read data is available.

## Setup Group (0x4)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | Set DMA block size | block len low, block len high | Sets up DMA block length
| All others       | Set time constant | time constant | Sets the sample timer to the provided time constant.

## RAM Playback Group (undocumented) (0x5)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | RAM Load | number of ram samples, ram playback count low, ram playback count high, unused, \<samples\> | Loads RAM with samples to play back, up to 64 bytes.  Playback count is the number of times the sample is repetitively played back in a loop.
| 0                | RAM Playback | none | Plays back samples in RAM

The "ram samples" argument contains the number of samples loaded into RAM.
The playback count (high and low) variables tell the sound card how many times
to play back the RAM sample.

## Reserved (0x6, 0xA, 0xB, 0xC)
The reserved command groups fall through to the status group 0 handler *iff* 
the DMA\_ENABLE\# pin is low. Otherwise the command is ignored and the
firmware waits for further commands from the DSP.

## Second Playback Group (0x7)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | Autoinit DMA DAC (ADPCM) | none | Starts auto-init DMA playback
| 2                | DMA DAC (ADPCM) | Length lo, length hi | Starts normal DMA playback

The above DMA commands check additional bits of the command byte as follows:

| Lower Nibble Bit | Description
| ---------------- | -------------
| 1                | 1=Use 2.6-bit ADPCM compression. 0=Use 4-bit ADPCM
| 0                | Read additional argument byte, use as starting reference for ADPCM

## Silence Playback Group (0x8)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| (Ignored)        | Silence playback | Length lo, length hi | Plays back silence.

## High Speed Group (0x9)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | High speed record | none | Starts high speed DMA record mode. Requires DSP reset to exit.
| All others       | High speed playback | none | Starts high speed DMA playback mode. Requires DSP reset to exit.

The above commands check an additional bit of the command byte as follows:

| Lower Nibble Bit | Description
| ---------------- | -------------
| 0                | 1=Exit after finishing. 0=Continuous until DSP reset.

The playback length is controlled by the current DMA block length. At the end
of a block, the interrupt is always asserted.

## Misc Command Group (0xD)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 2                | DMA continue | none | Reenables DMA and the sample timer
| 3 & !1           | Get speaker status | none | Returns 00 if muted, ff if not muted.
| 3 & 1            | Exit auto-init DMA | none | Cancels an ongoing auto-init DMA operation.
| 0 & !1           | Enable speaker | none | Turns on speaker output
| 0 & 1            | Disable speaker | none | Turns off speaker output
| All others       | DMA pause | none| Turns off DMA and halts the sample timer

## Version Group (0xE)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | Test read | none | Returns contents of test byte
| 2                | Test write | byte | Sets test byte to incoming data byte
| 1                | DMA code | byte | Takes byte, runs it through convoluted algorithm, spits it back out.
| 0                | DSP version | none | Returns major version byte (2) followed by minor version byte (2).
| All others       | DSP identification | byte | Returns bitwise complement of the incoming byte. 

## Auxiliary Group (0xF)
| Lower Nibble Bit | Command | Argument | Description
| ---------------- | ------- | -------- | -----------
| 3                | RAM test | none | Tests all internal SRAM by setting it to 0xAA and 0x55. Returns 0 if passing or address of failing byte.
| 2                | ROM checksum | none | Returns msb followed by lsb result of 16-bit checksum of the ROM. This routine has an off-by-one bug and increments the counter past the end of the 12-bit address space by one byte. Depending on the 8051, this either wraps back to 0 or continues on into unimplemented memory.
| 1                | Interrupt | none | Triggers an interrupt request
| 0                | Status | none | Returns a byte containing the contents of port2.
| All others       | Sine generator | none | Turns on sine wave output mode.

# Operating Descriptions

## Sample rate timer
This is implemented with an internal timer operating in mode 2 which is a timer
with automatic reload. The reload value corresponds to the value provided to
the "Set time constant" DSP command. Since the oscillator frequency is 12MHz,
the clock going to the timer is 1MHz.

## Data transfers
There are two mailboxes in the hardware: one for data going from the host to
the DSP and the other for data going from the DSP to the host. Each mailbox
has a flag that can be checked by the DSP. DAV\_DSP indicates when the host
has placed data in the incoming mailbox and it is safe for the DSP to read it.
DAV\_PC means that there is data in the outgoing mailbox and the host has not
read it yet.

To check for data from the PC, you busy-wait until DAV\_DSP gets set. Then you read the data byte with a MOVX instruction.

## Audio Playback
The sound card plays audio when it receives the playback command (0x14). This
command has the following structure:

1. Clear the DSP\_BUSY pin so that the host knows the DSP can receive
additional data bytes.
2. Check to see if DMA is enabled (DMA\_ENABLE#). If DMA is running (sound is
already playing back) then read a 16-bit value into the shadow length
variable, and then end the command early. The shadow length is how you can cue
up playback for the next playback command.
3. If DMA is not already enabled, then read back a 16-bit value from the host
and store it in the samples remaining counter.
4. Set the DSP busy pin so the host knows that playback is in progress.
5. Clear the DMA\_ENABLE# to ensure that the card can request DMA transfers
from the host.
6. In ADPCM mode, pulse DREQUEST high and wait for an 8-bit value to arrive
from the host. Prime the ADPCM receive buffer with this. In ADPCM reference
mode, instead of storing this byte in the receive buffer, send it to the DAC
output since this is the reference value.
7. Set up the proper interrupt vector for the timer interrupt handler. There
are separate handlers for 8-bit, ADPCM 2 bit, etc.
8. Enable timer interrupts, and turn on the sample timer.

When the timer interrupt triggers, the handler does the following:

1. Set DSP\_BUSY.
1. Check DAV\_DSP. If it is set, then there is a command pending from the
host, so immediately exit the handler. The main loop will handle it.
2. Pulse DREQUEST. Block and wait for a byte to come in. This is our sample.
3. Copy the sample byte to the DAC output.
4. Decrement the samples remaining counter.
5. If the samples remaining counter isn't zero, then clear DSP\_BUSY and exit
the handler.
6. Otherwise, check to see if the autoinit exit flag is set (controlled by the
exit autoinit DSP command). If so, then clear the autoinit exit flag, the dma
mode flag, and the autoinit mode flag. Clear the timer enable, turn it off,
pulse the host interrupt control line IREQUEST#, and set DMA\_ENABLE#. Exit
the handler.
7. Check the dma mode flag. If it's on, turn it off, turn off the autoinit
flag, then copy the shadow length over to the samples remaining counter. Pulse
the host IRQ IREQUEST#, clear the DSP\_BUSY pin, then exit the handler.
8. Check the dma autoinit mode flag. If it's off, then bail just the same way
that the autoinit exit flag ends the handler (clears flags, etc).
9. Otherwise copy the stored dma block length value to the samples remaining
counter. Pulse IREQUEST#, clear the DSP\_BUSY pin, and exit the handler.


## DMA Recording
The process of DMA recording is nearly identical to playback. The
initialization routine first has to mute the speaker since the DAC is shared
with the playback hardware.

In the timer interrupt handler, the firmware performs the ADC algorithm to get
a valid sample and places it in the mailbox for the PC DMA controller to read
it, then pulls the DREQUEST line to let the DMA controller know.

The ADC algorithm is a successive approximation technique that begins by
loading the half code value into the DAC. By looking at the comparator output,
the firmware chooses whether to use the upper half or the lower half of the
current partition, then divides that partition again, checking the comparator
again to decide which half to use, and so on and so forth until 8 bits have
been acquired.

Note: The DMA ADC command checks to see if DMA is already running. If so,
then it loads the length argument into length\_low (not used by the running
DMA operation). Otherwise it loads it into len\_left\_lo (Same for the high
byte.) This is so that you can queue up the length of the next set of samples
to record if DMA is already running, and get a glitch-free recording that
spans multiple DMA operations. 


## MIDI

This is not MPU-401 compatible--it is known as SB-MIDI mode. The firmware
configures the internal UART to use the standard MIDI baud rate and sets
up a 64-byte receive buffer. While in the Read Poll or MIDI UART command modes,
any bytes coming in on the serial port are placed in the receive buffer, which
uses the usual two pointer technique to maintain a read and write pointer.
As the host PC reads bytes, they are removed from the receive buffer.

The MIDI UART command never returns and requires a DSP RESET write. Basically
it immediately transmits any bytes received from the host PC over the UART,
and buffers any received UART bytes for the host PC.

### Timestamp mode
This mode bit, when set, uses the internal timer, set to tl0=17h, th0=fch.
When the timer overflow occurs, the ISR increments a 24-bit counter. This
counter gets output when you read MIDI data. It comes out as little-endian,
(low, mid, high bytes) followed by all MIDI data in the buffer. The counter and
timer get cleared when you run a MIDI read command, so it effectively gives you
the time delay between when you sent the DSP command and when the first data
comes back. These 3 bytes are stored in the same 64-byte buffer as the incoming
MIDI data.

When you run a MIDI command in timestamp mode, it sets a flag that is checked
in the timer interrupt handler. This flag is cleared when the command
terminates. The interrupt handler, if it sees the flag set, increments the
24-bit counter and resets the timer value to fc17h. This interrupt activity
replaces all other interrupt activity, which is why you can't play back or
record audio during MIDI transactions.

## Sine Wave Mode

TBD.

# Timer Interrupt Handlers

The timer interrupt handler depends on the current mode of the card. High speed
mode is checked first to minimize the interrupt latency.


| Handler          | Description
| --------------- | -----------
| MIDI timestamp | Increments the 24-bit MIDI timestamp counter
| High speed     | Handles high speed mode DAC and ADC
| DMA DAC8       | Outputs 8-bit uncompressed samples
| DMA DAC ADPCM2 | Outputs 2-bit ADPCM samples
| DMA DAC ADPCM4 | Outputs 4-bit ADPCM samples
| DMA DAC ADPCM26 | Outputs 2.6-bit ADPCM samples
| DAC silence | Outputs silence
| DMA ADC | Captures 8-bit uncompressed samples
| RAM playback | Plays back 8-bit uncompressed samples from RAM
| Sine | Plays back sine wave samples

