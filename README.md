# The Snark Barker - a SB 1.0 Clone

The Snark Barker is a 100% compatible clone of the famed SB 1.0 "Killer Card"
sound card from 1989. It implements all the features, including the digital
sound playback and recording, Ad Lib compatible synthesis, the joystick/MIDI
port, and the CMS chips (which are actually Philips SAA1099 synthesizer
devices).

![Snark Barker photo](https://github.com/schlae/snark-barker/blob/master/images/SnarkBarker.png)

All of the components are readily available. In the bill of materials,
Mouser part numbers are listed where they are available. Chips not available
from Mouser can be purchased from a variety of sources in China.

[Schematic](https://github.com/schlae/snark-barker/blob/master/SnarkBarker.pdf)

[Bill of Materials](https://github.com/schlae/snark-barker/blob/master/SnarkBarker.csv)

Please note that the 0.1" header pins are *not* listed on the BOM. They are
standard breakaway headers (both single and double row). Jumper shunts are
also not listed on the BOM.

[Fab files](https://github.com/schlae/snark-barker/blob/master/fab/SnarkBarker.zip)

Board dimensions are 9.1 x 4.2 inches. When ordering the board, you may want
to specify a card edge bevel (fairly cheap!) and selective gold plating
(expensive!) depending on your needs. The soldermask color can be whatever
you like, but hot pink is preferred.

## The Volume Knob
There don't seem to be any off-the-shelf knobs compatible with the Alps
potentiometer. You may be able to 3D print one based on the model below.
I'd recommend using a high-resolution SLA printer like the Formlabs Form 2.

Fasten it to the potentiometer using an M1.4x0.3mm thread, 6mm long screw
(McMaster-Carr part number 91800A036 or equivalent).

![Image of the volume knob](https://github.com/schlae/snark-barker/blob/master/images/vol_knob.png)

[Volume knob STEP model](https://github.com/schlae/snark-barker/blob/master/mech/vol_knob.zip)

Update: I've added a modified volume knob that may print better on FDM
printers.

[Volume knob STEP model, FDM version](https://github.com/schlae/snark-barker/blob/master/mech/vol_knob_fdm.zip)

## The ISA Card Bracket
The bracket specified in the BOM is a blank Keystone 9200 bracket. You will
need to punch or drill holes for the connectors. The KiCad board file has
detailed dimensions showing where to make the holes in the bracket.

I use a chassis nibbler tool to make the square slot for the volume knob as
well as the hole for the DA-15 joystick/MIDI connector. If you are rich,
Greenlee makes a punch for the DA-15 outline.

## The Firmware
There are two ways to get a programmed 80C51 chip for the Snark Barker. One
is to purchase a SB 2.0 DSP chip from China and put it in a 44-PLCC to
40-DIP adapter. This works fine and provides the largest feature set.

Another option is to buy a blank Atmel 89S51 (as listed in the BOM) and
program it with [this HEX file](https://github.com/schlae/snark-barker/blob/master/firmware/sb.hex).

## Assembly Notes
You may wish to socket the two CMS chips, the 80C51 microcontroller, and the
two Yamaha chips.

Be sure to add the 4.7K ohm bodge resistor on top of U5, running between
pins 4 and 14. (Shown below.)

![Photo of bodge resistor](https://github.com/schlae/snark-barker/blob/master/images/bodge.png)

For MIDI to work properly, you'll need to solder jumper wires on the headers
marked TXD and RXD (next to the SNARK BARKER logo). Originally these two 3-pin
headers may have been used as a debug port.

Be sure to place shunts in the jumpers marked DRQ1 and JP1, to enable DMA and
the joystick, respectively. Also place shunts to configure the I/O address and
IRQ.

## Other Notes
Like the original SB 1.0, the Snark Barker does not need a -5V rail.

## License
This work is licensed under a Creative Commons Attribution-ShareAlike 4.0
International License. See [https://creativecommons.org/licenses/by-sa/4.0/](https://creativecommons.org/licenses/by-sa/4.0/).

