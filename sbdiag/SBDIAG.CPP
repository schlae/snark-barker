//
// Snark Barker Diagnostic Program
// Copyright (c) 2020 Eric Schlaepfer
// This work is licensed under the Creative Commons Attribution-ShareAlike 4.0
// International License. To view a copy of this license, visit
// http://creativecommons.org/licenses/by-sa/4.0/ or send a letter to Creative
// Commons, PO Box 1866, Mountain View, CA 94042, USA.
//

// Need to implement:
//   CMS chips

// Compile using Borland Turbo C++ 3.1 using the Large memory model


#include <alloc.h>
#include <conio.h>
#include <dos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
#define __CPPARGS ...
#else
#define __CPPARGS
#endif

// Use Numerically-Controlled Oscillator to generate digital sine wave
#define USE_NCO 1

// Macros return upper or lower byte of a word.
#define HIGH(val) ((val) >> 8)
#define LOW(val) ((val) & 0xFF)

// Macros calculate the linear address and page of a far pointer.
#define LINEAR_ADDRESS(val) (((unsigned long)FP_SEG(val) << 4) + (unsigned long)FP_OFF(val))
#define PAGE(val) (LINEAR_ADDRESS(val) >> 16)

// Programmable Interrupt Controller registers and command.
#define PIC1_COMMAND 0x20
#define PIC1_MASK 0x21
#define PIC_EOI 0x20

// Offset between physical interrupts and logical interrupts.
#define INTERRUPT_OFFSET 0x8

// DMA controller macros to calculate address and length for a channel.
#define DMA_ADDRESS(channel) ((channel) * 2)
#define DMA_LENGTH(channel) ((channel) * 2 + 1)

// DMA controller registers and register bits.
#define DMA_MASK 0x0A
#define  DMA_MASK_SET 0x04
#define  DMA_MASK_CLEAR 0x00
#define DMA_MODE 0x0B
#define  DMA_MODE_READ 0x08
#define  DMA_MODE_WRITE 0x04
#define  DMA_MODE_SINGLE 0x40
#define  DMA_MODE_AUTO 0x10
#define DMA_CLEAR 0x0C

// Sound Blaster DSP I/O port offsets
#define DSP_RESET 0x6
#define DSP_READ 0xA
#define DSP_WRITE 0xC
#define DSP_WRITE_STATUS 0xC
#define DSP_READ_STATUS 0xE

// Sound Blaster DSP commands.
#define DSP_DMA_DAC 0x14
#define DSP_DIRECT_ADC 0x20
#define DSP_MIDI_READ_POLL 0x30
#define DSP_MIDI_WRITE_POLL 0x38
#define DSP_SET_TIME_CONSTANT 0x40
#define DSP_HALT_DMA 0xD0
#define DSP_ENABLE_SPEAKER 0xD1
#define DSP_DISABLE_SPEAKER 0xD3
#define DSP_VERSION 0xE1
#define DSP_WRITETEST 0xE4
#define DSP_READTEST 0xE8
#define DSP_SINE 0xF0
#define DSP_IRQ 0xF2
#define DSP_CHECKSUM 0xF4

// Macro to calculate Sound Blaster time constant from sample rate.
#define SAMPLE_RATE_TO_TC(a) (256 - (1000000 / (a)))

// Ad Lib stuff
#define ADLIB_INDEX_DELAY 6
#define ADLIB_WRITE_DELAY 36

// Ad Lib registers
#define ADLIB_STATUS    0x00
#define ADLIB_TEST      0x01
#define ADLIB_TIMER1    0x02
#define ADLIB_TIMER2    0x03
#define ADLIB_CONTROL   0x04
#define ADLIB_CSW       0x08
#define ADLIB_TVSKSR    0x20
#define ADLIB_KSLVOL    0x40
#define ADLIB_AD        0x60
#define ADLIB_SR        0x80
#define ADLIB_FREQ      0xA0
#define ADLIB_KEYBLOCK  0xB0
#define ADLIB_PERC      0xBD
#define ADLIB_OUT       0xC0
#define ADLIB_WAVE      0xE0

// CMS chip offsets
#define CMS_DATA1       0x00
#define CMS_REG1        0x01
#define CMS_DATA2       0x02
#define CMS_REG2        0x03

// CMS registers
#define CMS_AMPL0       0x00
#define CMS_FREQ0       0x08
#define CMS_OCT01       0x10
#define CMS_FE          0x14
#define CMS_NE          0x15
#define CMS_NG          0x16
#define CMS_EG0         0x18
#define CMS_ENABLE      0x1C


// RIFF WAVE file header.
typedef struct {
        char signature[4];     // 'RIFF'
        unsigned long length;  // Entire length of wave - 8 bytes.
        char format[4];        // 'WAVE'
        char fmt_id[4];        // 'fmt '
        unsigned long fmt_length; // Length of format struct.
        unsigned int audio_format;
        unsigned int channels;
        unsigned long sample_rate;
        unsigned long byte_rate;
        unsigned int block_align;
        unsigned int bits_per_sample;
} RIFF_FMT_CHUNK;

// RIFF wave file data header.
typedef struct {
        char signature[4]; // 'data'
        unsigned long length; // Length of waveform data.
} DATA_CHUNK;

// Menu item structure for menuing system
typedef struct {
        char name[32];
        void (*menufunc)();
        void *submenu;
} MENU_ITEM;

// Text mode info
struct text_info curtextmode;

// Sound Blaster port, interrupt, and DMA settings.
int sb_port = 0x220;
int sb_interrupt = 0x5;
int sb_dma = 0x1;

// Ad Lib and joystick port settings
int ad_port = 0x388;
int joy_port = 0x201;

// Saved interrupt handler and mask setting.
int old_pic_mask;
void interrupt (*old_interrupt_handler)(__CPPARGS);

// Flag set by interrupt service routine when interrupt gets handled.
volatile int interrupt_detected;

// DMA page register I/O ports in DMA channel order (0, 1, 2, 3).
const int dma_page_register[] = {0x87, 0x83, 0x81, 0x82};

// Sine wave table
unsigned char sine_table[256];


// Wait for data to become available from Sound Blaster DSP.
int wait_read(void)
{
        unsigned int i;
        for (i = 0; i < 1000; i++) {
                if (inportb(sb_port + DSP_READ_STATUS) & 0x80) {
                        return 1;
                }
                delay(1);
        }
        return 0; // Timed out.
}


// Wait for Sound Blaster DSP to be ready to receive a new byte.
int wait_write(void)
{
        unsigned int i;
        for (i = 0; i < 1000; i++) {
                if (!(inport(sb_port + DSP_WRITE_STATUS) & 0x80)) {
                        return 1;
                }
                delay(1);
        }
        return 0; // Timed out.
}


// Read data from Sound Blaster DSP.
int dsp_read(void)
{
        if (!wait_read()) {
                return -1;
        }
        return inportb(sb_port + DSP_READ);
}


// Write data byte or command to Sound Blaster DSP.
int dsp_write(int data)
{
        if (!wait_write()) {
                return 0;
        }
        outportb(sb_port + DSP_WRITE, data);
        return 1;
}


// Resets the DSP.
int dsp_reset(void)
{
        outportb(sb_port + DSP_RESET, 0x01);
        delay(1);
        outportb(sb_port + DSP_RESET, 0x00);
        if (dsp_read() == 0xAA) {
                return 1;
        }
        return 0;
}


// Commands the DSP to play 8-bit sound with the given number of samples.
int dsp_dma_dac(int length)
{
        if (!dsp_write(DSP_DMA_DAC)) {
                return 0;
        }
        if (!dsp_write(LOW(length - 1))) {
                return 0;
        }
        if (!dsp_write(HIGH(length - 1))) {
                return 0;
        }
        return 1;
}

// Commands the DSP to acquire a single 8-bit sample
int dsp_direct_adc()
{
        if (!dsp_write(DSP_DIRECT_ADC)) {
                return 0;
        }
        return dsp_read();
}


// Sets the DSP sampling rate, in hertz.
int dsp_set_time_constant(long sample_rate)
{
        if (!dsp_write(DSP_SET_TIME_CONSTANT)) {
                return 0;
        }
        if (!dsp_write(SAMPLE_RATE_TO_TC(sample_rate))) {
                return 0;
        }
        return 1;
}

// Stops a currently-running DMA operation
int dsp_dma_halt()
{
        return dsp_write(DSP_HALT_DMA);
}

// Turns on the Sound Blaster DSP speaker.
int dsp_enable_speaker()
{
        return dsp_write(DSP_ENABLE_SPEAKER);
}


// Turns off the Sound Blaster DSP speaker.
int dsp_disable_speaker()
{
        return dsp_write(DSP_DISABLE_SPEAKER);
}


// Returns the DSP version number.
int dsp_version(void)
{
        int major, minor;
        dsp_write(DSP_VERSION);
        major = dsp_read();
        minor = dsp_read();
        return (major << 8) | minor;
}

// Returns the DSP ROM checksum.
int dsp_checksum(void)
{
        int msb, lsb;
        dsp_write(DSP_CHECKSUM);
        msb = dsp_read();
        lsb = dsp_read();
        return (msb << 8) | lsb;
}

// Commands the DSP to trigger an 8-bit interrupt request.
int dsp_interrupt(void)
{
        return dsp_write(DSP_IRQ);
}

// Transmits MIDI bytes
int dsp_midi_write(int data)
{
        if (!dsp_write(DSP_MIDI_WRITE_POLL)) {
                return 0;
        }
        if (!dsp_write(data)) {
                return 0;
        }
        return 1;
}

// Receives MIDI byte
int dsp_midi_read()
{
        int data;
        if (!dsp_write(DSP_MIDI_READ_POLL)) {
                return -1;
        }
        data = dsp_read();
        if (!dsp_write(0x00)) { // Any byte ends the transaction
                return -1;
        }
        return data;
}

// Handles an interrupt from the Sound Blaster.
void interrupt interrupt_handler(__CPPARGS)
{
        // Acknowledge interrupt with DSP
        inportb(sb_port + DSP_READ_STATUS);

        // Send EOI to the PIC
        outportb(PIC1_COMMAND, PIC_EOI);

        // Set a flag so we know about it
        interrupt_detected = 1;
}


// Hooks the Sound Blaster interrupt, saving off the old handler.
void hook_interrupt()
{
        // Hook in new interrupt handler.
        old_interrupt_handler = getvect(sb_interrupt + INTERRUPT_OFFSET);
        setvect(sb_interrupt + INTERRUPT_OFFSET, interrupt_handler);

        // Update PIC mask to allow interrupts from this source.
        old_pic_mask = inportb(PIC1_MASK);
        outportb(PIC1_MASK, old_pic_mask & ~(1 << sb_interrupt));
}


// Restores the previous interrupt handler.
void unhook_interrupt()
{
        outportb(PIC1_MASK, old_pic_mask);
        setvect(sb_interrupt + INTERRUPT_OFFSET, old_interrupt_handler);
}


// Configures the DMA controller for playing back a sound.
void setup_dma_transfer(unsigned char far *buffer, int length, int autoinit)
{
        // Note that the input buffer cannot cross a page boundary or
        // DMA will loop back to the beginning of the page.
        unsigned int pointer = LINEAR_ADDRESS(buffer);
        unsigned int page = PAGE(buffer);

        // Mask off DMA while we set it up.
        outportb(DMA_MASK, DMA_MASK_SET | sb_dma);
        outportb(DMA_CLEAR, 0);
        outportb(DMA_MODE, (autoinit ? DMA_MODE_AUTO : DMA_MODE_SINGLE) | DMA_MODE_READ | sb_dma);
        outportb(DMA_ADDRESS(sb_dma), LOW((long)pointer));
        outportb(DMA_ADDRESS(sb_dma), HIGH((long)pointer));
        outportb(dma_page_register[sb_dma], page);
        outportb(DMA_LENGTH(sb_dma), LOW(length - 1));
        outportb(DMA_LENGTH(sb_dma), HIGH(length - 1));

        // Unmask DMA since we are ready to go.
        outportb(DMA_MASK, DMA_MASK_CLEAR | sb_dma);
}

// Stops and clears the DMA controller
void end_dma_transfer()
{
        outportb(DMA_MASK, DMA_MASK_SET | sb_dma);
        outportb(DMA_CLEAR, 0);
}

// Commands the Sound Blaster to play a sound.
void play_sound(unsigned char far *buffer, int length, int autoinit)
{
       setup_dma_transfer(buffer, length, autoinit);
       dsp_dma_dac(length);
}

// Allocate a block of memory that doesn't cross a page boundary.
void far *farmalloc_page(unsigned long nbytes)
{
        void far *start;
        void far *temp = NULL;
        unsigned int page_offset;
        // Find out where the next available memory is being allocated.
        start = farmalloc(1);
        if (start == NULL) {
                return NULL;
        }
        page_offset = LINEAR_ADDRESS(start);
#if 0
        printf("farmalloc_page: requested length %ld\n", nbytes);
        printf("farmalloc_page: ptr %Fp\n", start);
        printf("farmalloc_page: page offset %X\n", page_offset);
#endif

        // If we wouldn't have enough room for the buffer in the current
        // page, allocate a temporary buffer to fill up that space, so
        // that our real buffer will start at the beginning of the next
        // page. 
        if ((unsigned long)page_offset + nbytes - 1 > 0xFFFF) {
                temp = farmalloc(0xFFFF - page_offset + 1);
                if (temp == NULL) {
                        printf("farmalloc_page: Could not allocate temp buffer\n");
                        printf("farmalloc_page: farcore left: %ld\n", farcoreleft());
                        farfree(start);
                        return NULL;
                }
        }

        // Don't need the temporary buffer anymore. 
        farfree(start);

        // Allocate the real buffer.
        start = farmalloc(nbytes);
        page_offset = LINEAR_ADDRESS(start);
        if (temp != NULL) {
                farfree(temp);
        }
        return start;
}


// Cheesy WAV file parser.
unsigned char far *load_wave(const char *filename, unsigned long *length, unsigned int *sample_rate)
{
        FILE *fp;
        RIFF_FMT_CHUNK header;
        DATA_CHUNK data_header;
        unsigned char far *data;
        unsigned long i;

        fp = fopen(filename, "rb");
        if (fp == NULL) {
                printf("Couldn't open wave file.\n");
                return NULL;
        }

        fread(&header, sizeof(RIFF_FMT_CHUNK), 1, fp);
        if (header.signature[0] != 'R' ||
            header.signature[1] != 'I' ||
            header.signature[2] != 'F' ||
            header.signature[3] != 'F' ||
            header.format[0] != 'W' ||
            header.format[1] != 'A' ||
            header.format[2] != 'V' ||
            header.format[3] != 'E' ||
            header.fmt_id[0] != 'f' ||
            header.fmt_id[1] != 'm' ||
            header.fmt_id[2] != 't' ||
            header.fmt_id[3] != ' ') {
                printf("Not a wave file.\n");
                return NULL;
        }

        if (header.channels != 1 ||
            header.bits_per_sample != 8 ||
            header.audio_format != 1) {
                printf("Unsupported wave format.\n");
                return NULL;
        }

        // Skip to the start of the data chunk.
        fseek(fp, header.fmt_length + 20, SEEK_SET);

        fread(&data_header, sizeof(DATA_CHUNK), 1, fp);
        if (data_header.signature[0] != 'd' ||
            data_header.signature[1] != 'a' ||
            data_header.signature[2] != 't' ||
            data_header.signature[3] != 'a') {
                printf("Bad wave header.\n");
                return NULL;
        }

        data = (unsigned char far*)farmalloc_page(data_header.length);
        if (data == NULL) {
                printf("Couldn't allocate memory.\n");
                return NULL;
        }

        // fread() doesn't support far pointers. :-|
        for (i = 0; i < data_header.length; i++) {
                data[i] = fgetc(fp);
        }

        fclose(fp);
        *length = data_header.length;
        *sample_rate = header.sample_rate;
        return data;
}


// Parse 'SET BLASTER' from environment.
int parse_set_blaster()
{
        int i, val, temp_port, temp_interrupt, temp_dma;
        char *blaster = NULL;
        char *parameter;

        temp_port = temp_interrupt = temp_dma = -1;

        // Find the SET BLASTER statement.
        for (i = 0; environ[i] != NULL; i++) {
                if (strstr(environ[i], "BLASTER")) {
                        blaster = environ[i];
                        break;
                }
        }

        if (!blaster) {
                return 0;
        }

        // Parse the SET BLASTER statement.
        parameter = strtok(blaster, "=");
        while (1) {
                parameter = strtok(NULL, " ");
                if (!parameter) {
                        break;
                }
                switch(parameter[0]) {
                        case 'A':
                                sscanf(&parameter[1], "%x", &temp_port);
                                break;
                        case 'I':
                                temp_interrupt = atoi(&parameter[1]);
                                break;
                        case 'D':
                                temp_dma = atoi(&parameter[1]);
                                break;
                        default:
                                break;
                }
        }

        if (temp_port < 0 || temp_interrupt < 0 || temp_dma < 0) {
                printf("Malformed SET BLASTER statement.\n");
                return 0;
        }

        sb_port = temp_port;
        sb_interrupt = temp_interrupt;
        sb_dma = temp_dma;
        return 1;
}


// Ensures that the Sound Blaster shows up at the specified port.
int check_port()
{
        int retval;
        retval = dsp_reset();
        if (!retval) {
                return 0;
        }
        printf("Sound Blaster detected, DSP version %X\n", dsp_version());
        printf("DSP Checksum %X\n", dsp_checksum());
        return 1;
}


// Ensures that the interrupt is working.
int check_interrupt()
{
        int retval;
        interrupt_detected = 0;
        retval = dsp_interrupt();
        if (!retval) {
                return 0;
        }
        delay(10);
        return interrupt_detected;
}

// Ad Lib routines
void adlib_write(int reg, int val)
{
        int i;
        outportb(ad_port, reg);
        for (i = 0; i < ADLIB_INDEX_DELAY; i++) inportb(ad_port);
        outportb(ad_port + 1, val);
        for (i = 0; i < ADLIB_WRITE_DELAY; i++) inportb(ad_port);
}

// Gets the status byte
int adlib_status()
{
        return inportb(ad_port);
}

// Tries to detect an Ad Lib using the YM3812 timer feature
int adlib_detect()
{
        int status1, status2;
        adlib_write(ADLIB_CONTROL, 0x60); // Reset timers 1 and 2
        adlib_write(ADLIB_CONTROL, 0x80); // Reset IRQ flag
        status1 = adlib_status();
        adlib_write(ADLIB_TIMER1, 0xFF); // Set the timer to 0xFF, fast rollover
        adlib_write(ADLIB_CONTROL, 0x21); // Start timer1
        delay(1);
        status2 = adlib_status();
        if ((status1 & 0xE0) == 0x00 && (status2 & 0xE0) == 0xC0) {
                printf("Ad Lib detected!\n");
                return 1;
        }
        printf("Ad Lib not detected. First status read: %x. Second status read: %x.\n", status1, status2);
        return 0;
}

const char op_regs_table[] = {0x20, 0x40, 0x60, 0x80, 0xE0};
const char op_table[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5,
                         0x8, 0x9, 0xA, 0xB, 0xC, 0xD,
                         0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
void adlib_op(int op, const char op_reg[5])
{
        int i;
        for (i = 0; i < 5; i++) {
                adlib_write(op_regs_table[i] + op_table[op], op_reg[i]);
        }
}

// Turns the key on for a specific channel
void adlib_keyon(int channel, int freq, int am, int on)
{
        int block;
        int reg;
        // Compute register values given this frequency value
        // Compute block number, yes, this is cheesy.
        if (freq < 194) {
                block = 2;
        } else if (freq <= 388) {
                block = 3;
        } else if (freq <= 777) {
                block = 4;
        } else if (freq <= 1554) {
                block = 5;
        } else if (freq <= 3107) {
                block = 6;
        } else if (freq <= 6215) {
                block = 7;
        } else
                block = 8;
        reg = (long)freq * ((long)1 << (20 - block)) / 49716;
                
        adlib_write(ADLIB_FREQ + channel, LOW(reg));
        adlib_write(ADLIB_OUT + channel, 0x30 | (am & 1));
        adlib_write(ADLIB_KEYBLOCK + channel, (on ? 0x20 : 0) | (HIGH(reg) & 0x3) | (block << 2) );
}

void adlib_keyoff(int channel)
{
        adlib_write(ADLIB_KEYBLOCK + channel, 0);
}

#define NUM_NAMES 30
const char *names[] = {"Snark Barker", "Snood Bloober", "Sound Baster",
                       "Sad Bluster", "Sod Duster", "Sand Duster",
                       "Sun Buster", "Sound Sphincter", "Sound Stinker",
                       "Sound Bladder", "Sound Bleater", "Sow Blaster",
                       "Snud Bluffer", "Snard Blarger", "Snorp Blorper",
                       "Sud Buggler", "Snot Blotter", "Sneet Beeper",
                       "Snoot Booper", "Soup Blooper", "Snout Flouter",
                       "Snake Baker", "Spung Bunger", "Star Barper",
                       "Snood Booger", "Sound Barker", "Sound Basher",
                       "Snort Snorter", "Ear Blaster", "Sound Bluster"};

const char *getbanner(void)
{
        int i;
        static int selected = random(NUM_NAMES);
        return names[selected];
}

void blank_line()
{
        int i;
        for (i = 0; i < curtextmode.screenwidth; i++) {
                cputs(" ");
        }
}

void do_statusbar()
{
        int h = curtextmode.screenheight;
        int w = curtextmode.screenwidth;
        window(1, 1, w, 1);
        textattr(0x1f);
        blank_line();
        cprintf(" *** %s Diagnostics Program ***", getbanner());
        window(1, h, w, h);
        blank_line();
        cprintf("Current SB I/O %x Int %d DMA %d. Ad Lib: %x. Joystick: %x.",
                sb_port, sb_interrupt, sb_dma, ad_port, joy_port);
        window(1, 3, w, h - 1);
        textattr(0x7);
}

int get_user_int(int min, int max)
{
        int oldx = wherex();
        int oldy = wherey();

        int v;
        char ent[32];
        ent[0] = 32;
        clreol();
        while (1) {
                cgets(ent);
                sscanf(&ent[2], "%x", &v);
                if (v >= min && v <= max) break;
                gotoxy(oldx, oldy);
        }
        return v;
}

void menu_set_sb_port()
{
        printf("Enter the new SB base address: ");
        sb_port = get_user_int(0x0, 0xFFF);
        do_statusbar();
}

void menu_set_sb_int()
{
        printf("Enter the new SB interrupt: ");
        sb_interrupt = get_user_int(1, 7);
        do_statusbar();
}

void menu_set_sb_dma()
{
        printf("Enter the new SB DMA: ");
        sb_dma = get_user_int(0, 3);
        do_statusbar();
}

void menu_set_ad_port()
{
        printf("Enter the new Ad Lib base address: ");
        ad_port = get_user_int(0x0, 0xFFF);
        do_statusbar();
}

void menu_set_joy_port()
{
        printf("Enter the new joystick base address: ");
        joy_port = get_user_int(0x0, 0xFFF);
        do_statusbar();
}

void menu_joy_raw()
{
        int y;
        char joybits;
        char key;
        printf("Joystick raw status test.\n");
        printf("Press spacebar to reset the NE558.\n");
        printf("Press any other key to end the test.\n");
        printf("\nButton bits: 0=closed, 1=open\n");
        printf("Coordinate bits: 0=timeout, 1=charging\n");
        y = wherey();
        while(1) {
                joybits = inportb(joy_port);
                gotoxy(1, y);
                printf("(bit 7) Button 4: %d\n", (joybits >> 7) & 1);
                printf("(bit 6) Button 3: %d\n", (joybits >> 6) & 1);
                printf("(bit 5) Button 2: %d\n", (joybits >> 5) & 1);
                printf("(bit 4) Button 1: %d\n", (joybits >> 4) & 1);
                printf("(bit 3) BY: %d\n", (joybits >> 3) & 1);
                printf("(bit 2) BX: %d\n", (joybits >> 2) & 1);
                printf("(bit 1) AY: %d\n", (joybits >> 1) & 1);
                printf("(bit 0) AX: %d\n", joybits & 1);
                if (kbhit()) {
                        key = getch();
                        if (key == ' ') {
                                outportb(joy_port, 0xAA); // Value doesn't matter
                        } else {
                                break;
                        }
                }
        }
        clrscr();
}


void joy_read(int pot[4])
{
        int i, p;
        int mask = 0xF;
        pot[0] = pot[1] = pot[2] = pot[3] = 0;
        disable();
        outportb(joy_port, 0x55); // Value doesn't matter
        // Bit goes low when the RC circuit has finished charging.
        for (i = 0; i < 10000; i++) {
                p = inportb(joy_port) ^ mask;
                if (p & 1) {
                        pot[0] = i;
                        mask ^= 1;
                }
                if (p & 2) {
                        pot[1] = i;
                        mask ^= 2;
                }
                if (p & 4) {
                        pot[2] = i;
                        mask ^= 4;
                }
                if (p & 8) {
                        pot[3] = i;
                        mask ^= 8;
                }
                if (!mask) break;
        }
        enable();
}

// Measure joystick potentiometer values
void menu_joypot()
{
        int y;
        int pot[4];
        printf("Joystick potentiometer test.\n");
        printf("Press any key to end the test.\n");
        y = wherey();
        do {
                gotoxy(1, y);
                joy_read(pot);
                clreol();
                printf("Joystick A - X: %d, Y: %d\n", pot[0], pot[1]);
                clreol();
                printf("Joystick B - X: %d, Y: %d\n", pot[2], pot[3]);
                delay(10); // So we don't totally lock up interrupts

        } while (!kbhit());
        getch();
        clrscr();
}

void menu_dsp_addr()
{
        printf("SB DSP Address Test.\n");
        printf("Check for pulses on pin 1 of U19 (ISA_READ#).\n");
        printf("Press any key to end the test.\n");
        do {
                inportb(sb_port + DSP_READ);
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_dsp_write()
{
        printf("SB DSP Write Test.\n");
        printf("Check for pulses on pin 11 of U18 (ISA_WRITE#).\n");
        printf("Also check for pulses on pin 1 of U18 (DSP_READ#), which checks if\n");
        printf("the DSP is responding to received commands. Check pin 28 of U13 to\n");
        printf("make sure the DSP can tell a byte has arrived (DAV_DSP).\n");   
        printf("Press any key to end the test.\n");
        do {
                dsp_write(DSP_DISABLE_SPEAKER); // Single byte command with no return arguments
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_dsp_reset()
{
        int retval = dsp_reset();
        printf("SB DSP Reset Command.\n");
        if (retval) {
                printf("Good, SB DSP responded with 0xAA.\n");
        } else {
                printf("The SB DSP did not respond as expected.\n");
        }
        printf("Press any key to return to the menu.\n");
        getch();
        clrscr();
}

void menu_dsp_ver()
{
        int y;
        int ver;
        printf("SB DSP Version.\n");
        printf("This test repeatedly returns the ID of the SB.\n");
        printf("Press any key to end the test.\n");
        y = wherey();
        do {
                gotoxy(1, y);
                ver = dsp_version();
                printf("SB DSP version: %d.%02d", HIGH(ver), LOW(ver));
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_dsp_int()
{
        int y;
        int result;
        printf("SB DSP Interrupt Test.\n");
        printf("This test repeatedly manually triggers the SB interrupt.\n");
        printf("Press any key to end the test.\n");
        hook_interrupt();
        y = wherey();
        do {
                gotoxy(1, y);
                result = check_interrupt();
                if (result) {
                        printf("OK! Interrupt fired. :-)");
                } else {
                        printf("No interrupt fired. :-(");
                }
                gotoxy(1, y);
        } while (!kbhit());
        unhook_interrupt();
        getch();
        clrscr();
}

void menu_dsp_sine()
{
        int retval;
        printf("SB DSP Sine Wave.\n");
        printf("This test uses the DSP to generate a sine wave.\n");
        printf("It does not require DMA or interrupts.\n");
        printf("Press any key to end the test.\n");
        dsp_enable_speaker(); // Only required for SB16+
        retval = dsp_write(DSP_SINE);
        if (!retval) {
                printf("(DSP write did not succeed, you likely did not hear anything.\n");
        }
        do {
        } while (!kbhit());
        dsp_reset();
        getch();
        clrscr();
}

// Plays a digital sample in a loop. Quits when key is pressed.
void loop_play(unsigned char far *buffer, int length, int sample_rate)
{
        dsp_set_time_constant(sample_rate);
        dsp_enable_speaker();
        hook_interrupt();
        // Play it in a loop!
        do {
                interrupt_detected = 0;
                play_sound(buffer, length, 0);
                // Wait for playback to complete before trying again
                while(!interrupt_detected && !kbhit()) {}
        } while (!kbhit());
        getch();
        dsp_dma_halt();
        end_dma_transfer();
        dsp_disable_speaker();
        unhook_interrupt();
}

void menu_dsp_play()
{
        int retval;
        unsigned char far *buffer;
        unsigned long size;
        unsigned int sample_rate;

        printf("SB DSP Digital Audio Playback.\n");
        printf("This test plays back a sound file using DMA and\n");
        printf("and interrupts. If you only hear the sound play\n");
        printf("once, it means that the interrupt isn't working.\n");
        printf("Press any key to end the test.\n");

        buffer = load_wave("TADA.WAV", &size, &sample_rate);
        if (buffer == NULL) {
                printf("Could not load TADA.WAV! Is it in the current directory?\n");
                printf("Press any key to go back to the menu.\n");
                getch();
                clrscr();
                return;
        }
        loop_play(buffer, size, sample_rate);
        farfree(buffer);
        clrscr();
}

void menu_dsp_sinedma()
{
        int retval, i;
        unsigned int acc = 0;
        unsigned int incr;
        unsigned char far *buffer;
        const unsigned long size = 22050;
        const unsigned int sample_rate = 22050;

        printf("SB DSP Sine Wave (uses DMA).\n");
        printf("This test plays back a sine wave using DMA and\n");
        printf("and interrupts. If you only hear a short tone\n");
        printf("then the interrupt didn't work. Hearing periodic\n");
        printf("glitches is normal because this test does not\n");
        printf("use auto-init DMA.\n");
        printf("Press any key to end the test.\n");

        buffer = (unsigned char *far)farmalloc_page(size);
        if (buffer == NULL) {
                printf("Could not allocate buffer.\n");
                printf("Press any key to go back to the menu.\n");
                getch();
                clrscr();
                return;
        }
#if USE_NCO
        incr = (unsigned int)((1000ul << 16)/(unsigned long)sample_rate);
        for (i = 0; i < size; i++) {
                acc += incr;
                buffer[i] = sine_table[HIGH(acc)];
        }
#else
        for (i = 0; i < size; i++) {
                buffer[i] = 127 + (127.0 * sin(2 * M_PI * 1000 * i / sample_rate));
        }
#endif
        loop_play(buffer, size, sample_rate);
        farfree(buffer);
        clrscr();
}

void menu_dsp_mic()
{
        int y, i;
        int result;
        printf("SB DSP Microphone Test.\n");
        printf("This test reads the microphone input and displays it as a\n");
        printf("bargraph. No DMA or interrupts are required.\n");
        printf("Press any key to end the test.\n");
        dsp_disable_speaker();
        y = wherey();
        do {
                gotoxy(1, y);
                clreol();
                result = dsp_direct_adc() - 127;
                // Get the absolute magnitude
                if (result < 0) result = -result;
                // Scale result.
                for (i = 0; i < (result >> 1); i++) {
                        putch(219);
                }
                printf("%i", result);
                delay(20);
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_ad_detect()
{
        int y;
        printf("Ad Lib Detection Test.\n");
        printf("This attempts to detect an Ad Lib/YM3812 by using the timer\n");
        printf("feature of the chip. Both reads and writes need to be functioning\n");
        printf("for this test to work. The test repeats until you hit a key.\n");
        printf("Press any key to end the test.\n");
        y = wherey();
        do {
                gotoxy(1, y);
                adlib_detect();
                
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_ad_sine()
{
        int i;
        const char sine_op[] = {0x21, 0x00, 0xf0, 0x0f, 0x00};
        printf("Ad Lib Sine Wave Generator.\n");
        printf("This programs the YM3812 to generate a sine wave.\n");
        printf("The registers are written only once.\n");
        printf("Press any key to end the test.\n");

        // Program registers
        adlib_write(ADLIB_TEST, 0x20); // Use WSE
        adlib_write(ADLIB_CSW, 0); // No CSW
        adlib_write(ADLIB_PERC, 0); // Melodic mode
        adlib_op(0, sine_op); // Set up operator 0
        adlib_keyon(0, 1000, 1, 1); // 1000Hz for channel 0, AM mode on

        getch();
        adlib_keyoff(0);
        clrscr();
}

void menu_ad_loop()
{
        int i;
        //                       TVS   OUT     AD    SR    WS
        const char bass_op1[] = {0x01, 0x00, 0xC5, 0x08, 0x00};
        const char bass_op2[] = {0x01, 0x00, 0xC5, 0x08, 0x00};
        const char snare_op[] = {0x01, 0x00, 0xF5, 0x08, 0x00};
        printf("Ad Lib Percussion Loop.\n");
        printf("This programs the YM3812 to generate a looping\n");
        printf("percussion sound.\n");
        printf("Press any key to end the test.\n");

        // Program registers
        adlib_write(ADLIB_TEST, 0x20); // Use WSE
        adlib_write(ADLIB_CSW, 0); // No CSW

        adlib_op(12, bass_op1);
        adlib_op(15, bass_op2);
        adlib_op(16, snare_op);
        adlib_keyon(6, 30, 0, 0); // For bass drum

        do {
                adlib_write(ADLIB_PERC, 0x30);
                delay(200);
                adlib_write(ADLIB_PERC, 0x20);
                delay(200);
                adlib_write(ADLIB_PERC, 0x28);
                delay(200);
                adlib_write(ADLIB_PERC, 0x20);
                delay(200);
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_midi_transmit()
{
        printf("MIDI Transmit Test.\n");
        printf("This transmits MIDI bytes over the SB-MIDI port.\n");
        printf("The pattern is 0xAA 0x55... The test repeats until you hit a key.\n");
        printf("Press any key to end the test.\n");
        do {
                dsp_midi_write(0xAA);
                dsp_midi_write(0x55);
        } while (!kbhit());
        getch();
        clrscr();
}

void menu_midi_loopback()
{
        int y, i;
        int result;
        printf("MIDI Loopback Test.\n");
        printf("This test transmits bytes over the SB-MIDI port.\n");
        printf("If an external loopback is connected, it checks received data.\n");
        printf("Connect a wire from pin 12 to pin 15 of the DA-15 joystick connector.\n");
        printf("Press any key to end the test.\n");
        y = wherey();
        do {
                gotoxy(1, y);
                dsp_midi_write(0xAA);
                result = dsp_midi_read();
                clreol();
                if (result != 0xAA) {
                        printf("Data did not match 0xAA, it was %x instead!", result);
                } else {
                        printf("OK!");
                }
                delay(20);
        } while (!kbhit());
        getch();
        clrscr();
}


void cms_write(int reg, int data)
{
        // CMS chips have a DTACK so in theory
        // we don't need to have delays.
        if (reg & 0x100) {
                outportb(sb_port + CMS_REG2, reg);
                outportb(sb_port + CMS_DATA2, data);
        } else {
                outportb(sb_port + CMS_REG1, reg);
                outportb(sb_port + CMS_DATA1, data);
        }
}

void cms_writeloop(int chip)
{

        int y, i;
        int result;
        printf("CMS Write Loop Test for ");
        if (!chip) {
                printf("CMS Chip #1 (U15).\n");
        } else {
                printf("CMS Chip #2 (U14).\n");
        }
        printf("This test repeatedly writes to a register in the\n");
        printf("selected CMS chip. You will not hear anything, but\n");
        printf("you should see activity on the CS# (pin 2), WR# (pin 1),\n");
        printf("A0 (pin 3), and DTACK# (pin 7) signals on the chip.\n");

        printf("Press any key to end the test.\n");
        do {
                cms_write(CMS_AMPL0 | chip, 0);
        } while (!kbhit());
        getch();
        clrscr();
}

void cms_sine(int channels)
{

        int chip = ((channels & 4)) ? 0x100 : 0;
        printf("CMS Chip Test.\n");
        printf("This test generates a sine wave using CMS chip #");
        if (!chip) {
                printf("1 (U15).\n");
        } else {
                printf("2 (U14).\n");
        }
        printf("You should hear sound from ");
        if ((channels & 3) == 3) {
                printf("both channels.\n");
        } else if ((channels & 3) == 1) {
                printf("the left channel.\n");
        } else if ((channels & 3) == 2) {
                printf("the right channel.\n");
        }
        printf("Press any key to end the test.\n");
        // Set up SAA1099
        cms_write(CMS_AMPL0 | chip, (channels & 1 ? 0xf : 0) |
                                    (channels & 2 ? 0xf0 : 0 )); // Maximum amplitude
        // Frequency input is 7.159090MHz.
        // Note = 512 - (((7159090 / 512) * 2^octave) / frequency)
        cms_write(CMS_FREQ0 | chip, 64); // 1000.991Hz
        cms_write(CMS_OCT01 | chip, 5);  // 976-1950Hz octave
        cms_write(CMS_EG0 | chip, 0);    // No envelope generator
        cms_write(CMS_FE | chip, 0x1);   // Turn on sine generator
        cms_write(CMS_NE | chip, 0);     // Turn off noise generator

        cms_write(CMS_ENABLE | chip, 0x1);   // Enable the chip's output
        getch();
        cms_write(CMS_ENABLE | chip, 0x0);
        clrscr();

}

void menu_cms1_loop()
{
        cms_writeloop(0);
}

void menu_cms2_loop()
{
        cms_writeloop(0x100);
}

void menu_cms1_sine()
{
        cms_sine(3);
}

void menu_cms1_left()
{
        cms_sine(1);
}

void menu_cms1_right()
{
        cms_sine(2);
}

void menu_cms2_sine()
{
        cms_sine(4 | 3);
}

void menu_cms2_left()
{
        cms_sine(4 | 1);
}

void menu_cms2_right()
{
        cms_sine(4 | 2);
}


// Menu definitions.
const MENU_ITEM menu_dsptests[] = { {"DSP Address Test", menu_dsp_addr},
                                    {"DSP Write Test", menu_dsp_write},
                                    {"DSP Reset", menu_dsp_reset},
                                    {"Get DSP Version", menu_dsp_ver},
                                    {"Check DSP Interrupt", menu_dsp_int},
                                    {"Generate Sine Wave (no DMA)", menu_dsp_sine},
                                    {"Generate Sine Wave (DMA)", menu_dsp_sinedma},
                                    {"Play Digital Audio", menu_dsp_play},
                                    {"Check Microphone Input", menu_dsp_mic},
                                    {0, 0} };
const MENU_ITEM menu_adlibtests[] = { {"Try to Detect Ad Lib", menu_ad_detect},
                                      {"Play Sine Wave", menu_ad_sine},
                                      {"Play Percussion Loop", menu_ad_loop},
                                      {0, 0} };
const MENU_ITEM menu_cmstests[] = { {"Write loop (CMS 1)", menu_cms1_loop},
                                    {"Write loop (CMS 2)", menu_cms2_loop},
                                    {"(CMS 1) Generate sine wave", menu_cms1_sine},
                                    {"(CMS 1) Generate sine wave (L)", menu_cms1_left},
                                    {"(CMS 1) Generate sine wave (R)", menu_cms1_right},
                                    {"(CMS 2) Generate sine wave", menu_cms2_sine},
                                    {"(CMS 2) Generate sine wave (L)", menu_cms2_left},
                                    {"(CMS 2) Generate sine wave (R)", menu_cms2_right},
                                    {0, 0} };
const MENU_ITEM menu_miditests[] = { {"MIDI Transmit Test", menu_midi_transmit},
                                     {"MIDI Loopback Test", menu_midi_loopback},
                                     {0, 0} };
const MENU_ITEM menu_joytests[] = { {"Get raw port status", menu_joy_raw},
                                    {"Measure joystick potentiometers", menu_joypot},
                                    {0, 0} };
const MENU_ITEM menu_ports[] = { {"Set SB base address", menu_set_sb_port},
                                 {"Set SB interrupt", menu_set_sb_int},
                                 {"Set SB DMA", menu_set_sb_dma},
                                 {"Set Ad Lib base address", menu_set_ad_port},
                                 {"Set joystick base address", menu_set_joy_port},
                                 {0, 0} };

const MENU_ITEM menu_main[] = { {"DSP Tests", 0, (void *)menu_dsptests},
                                {"Ad Lib/FM Synth Tests", 0, (void *)menu_adlibtests},
                                {"CMS Chip Tests", 0, (void *)menu_cmstests},
                                {"SBMIDI Port Tests", 0, (void *)menu_miditests},
                                {"Joystick Port Tests", 0, (void *)menu_joytests},
                                {"Change Port Settings", 0, (void *)menu_ports},
                                {0, 0} };


// Navigate menu system. Submenus are done recursively.
void display_menu (const MENU_ITEM *menu, char *banner, int top)
{
        int i;
        char ch;

        clrscr();
        while (1) {
                printf("%s\n", banner);
                i = 0;
                do {
                        printf(" %d.  %s\n", i, menu[i].name);
                        i++;
                } while (menu[i].menufunc != 0 || menu[i].submenu != 0);
                if (top) {
                        printf("\nEnter selection or press Esc to quit.");
                } else {
                        printf("\nEnter selection or press Esc to return to the previous menu.");
                }
                do {
                        ch = getch();
                        if (ch == 27) return;
                } while (ch < '0' || ch >= ('0' + i));
                printf("\n");
                i = ch - '0';
                if (menu[i].submenu) {
                        display_menu((MENU_ITEM *)menu[i].submenu,
                                     (char *)menu[i].name, 0);
                        clrscr();
                } else {
                        menu[i].menufunc();
                        gotoxy(1, 1);
                }
        }
}

void load_sine_table()
{
        int i;
        for (i = 0; i < 64; i++) {
                sine_table[i] = 127 + (127.0 * sin(M_PI * i / 128));
                sine_table[127 - i] = sine_table[i];
                sine_table[i + 128] = 255 - sine_table[i];
                sine_table[255 - i] = 255 - sine_table[i];
        }
}

// Where we begin.
int main()
{
        randomize();
#if USE_NCO
        load_sine_table();
#endif
        gettextinfo(&curtextmode);

        // Check for BLASTER environment variable, preset parameters
        parse_set_blaster();

        // Set up screen
        clrscr();
        do_statusbar();

        // Launch the menu
        display_menu(menu_main, "Main Menu", 1);

        // Clean up
        window(1, 1, curtextmode.screenwidth, curtextmode.screenheight);
        clrscr();
        return 0;
}
