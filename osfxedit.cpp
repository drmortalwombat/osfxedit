#include <c64/kernalio.h>
#include <c64/sid.h>
#include <c64/keyboard.h>
#include <c64/memmap.h>
#include <c64/cia.h>
#include <c64/vic.h>
#include <c64/rasterirq.h>
#include <c64/sprites.h>
#include <audio/sidfx.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static char * const Screen = (char *)0x8000;
static char * const Sprites = (char *)0x8500;
static char * const Hires = (char *)0xa000;
static char * const ROMFont = (char*)0xd000;
static char * const Color = (char *)0xd800;

static const char sprite_img_base = ((Sprites - Screen) / 64);

static const char voice = 2; // can sample envelope on voice 3 if needed for validation

static const char max_neffects = 15;

#ifdef OSFXEDIT_USE_NMI
// define this to enable a non-50Hz rate of calling sfx_loop()
const char nmi_start_rasterline = 100;
#ifdef OSFXEDIT_NMI_CYCLES
const unsigned nmi_cycles = OSFXEDIT_NMI_CYCLES;
#else
// pico8 runs at 22,050 / 183 = 120.491803279 notes/second or 8.29932 ms/note
// this must be our music and SFX tick rate
// on a PAL C64 that is 985248Hz / 120.491803279 = 8176.89 cycles ~ 8177cycles
// modified to 8189 to stablise against the raster beam - no rolling
// adjust as necessary
const unsigned nmi_cycles = 8189;
#endif
#endif

char menubuf[40];

// for use before message displaye, mainly for filename
void save_menu(void)
{
	char* dp = Screen + (max_neffects + 1) * 40;
	for (char i = 0; i < 40; i++) menubuf[i] = dp[i];
}

// for use before message displaye, mainly for filename
void restore_menu(void)
{
	char* dp = Screen + (max_neffects + 1) * 40;
	char* cp = Color + (max_neffects + 1) * 40;
	for (char i = 0; i < 40; i++)
	{
		dp[i] = menubuf[i];
		cp[i] = VCOL_LT_BLUE;
	}
}

char keyb_queue, keyb_repeat;
char csr_cnt;
char irq_cnt;
char msg_cnt;

__interrupt void isr(void)
{
	csr_cnt++;
    if (msg_cnt) {
      msg_cnt--;
      if (msg_cnt == 0) restore_menu();
    }
#ifndef OSFXEDIT_USE_NMI
	irq_cnt++;
	// vic.color_border = VCOL_LT_BLUE;
	sidfx_loop_2();
#endif

	// vic.color_border = VCOL_LT_BLUE;
	keyb_poll();
	// vic.color_border = VCOL_ORANGE;

	if (!keyb_queue)
	{
		if (!(keyb_key & KSCAN_QUAL_DOWN))
		{
			if (keyb_repeat)
				keyb_repeat--;
			else
			{
				if (key_pressed(KSCAN_CSR_RIGHT))
				{
					if (key_shift())
						keyb_queue = KSCAN_CSR_RIGHT | KSCAN_QUAL_DOWN | KSCAN_QUAL_SHIFT;
					else
						keyb_queue = KSCAN_CSR_RIGHT | KSCAN_QUAL_DOWN;
					keyb_repeat = 2;
				}
			}
		}
		else
		{
			keyb_queue = keyb_key;
			keyb_repeat = 20;
		}
	}
	// vic.color_border = VCOL_BLACK;
}

#ifdef OSFXEDIT_USE_NMI
__interrupt void nmi_isr(void) {
  irq_cnt++;
  sidfx_loop_2();
}

void nmi_isr_stub(void) {
  __asm {
        pha
        txa
        pha
        tya
        pha

        lda $dd0d    // cia2.icr ack nmi int

        tsx
        lda $0104,x // peek at status register before NMI occured: 3 pushes above + 1 + SP
        and #$04    // test "I" bit
        bne go_isr  // if interrupts were already disabled before NMI occured
                    // then don't re-enable them, as that will likely cause a 2nd re-entrant interrupt

        cli         // allow normal interrupts

go_isr:
        jsr nmi_isr
        pla
        tay
        pla
        tax
        pla
        rti
  }
}

#endif

RIRQCode	rirq_isr, rirq_mark0, rirq_mark1, rirq_bitmap;

const SIDFX basefx = {
	1000, 2048, 
	SID_CTRL_GATE | SID_CTRL_SAW,
	SID_ATK_8 | SID_DKY_24,
	0x80 | SID_DKY_204,
	0, 0,
	4, 0,
	0
};

SIDFX	effects[max_neffects];

char	neffects = 1;

const char SidHead[] = S"  TSRNG FREQ  PWM  ADSR DFREQ DPWM T1 T0";
const char SidRow[]  = S"# TSRNG 00000 0000 0000 00000 0000 00 00";
const char MenuRow[] = S"LOAD SAVE NEW  D09  [..............]    ";

const char HexDigit[] = S"0123456789ABCDEF";


void uto5digit(unsigned u, char * d)
{
	for(signed char i=4; i>=0; i--)
	{
		d[i] = u % 10 + 48;
		u /= 10;
	}
}

char cursorX, cursorY;
char drive = 9; // vice defaults to an iecdrive9 on host file system, which is a convenient use case

void showdrive(void)
{
	char* dp = Screen + (max_neffects + 1) * 40;
	char* cp = Color + (max_neffects + 1) * 40;
    
    char fs[6];
	uto5digit(drive, fs);
	for (char i = 0; i < 2; i++)
	{
		dp[16 + i] = fs[i + 3];
		cp[16 + i] = VCOL_LT_BLUE;
	}
}

void showmenu(void)
{
	char* dp = Screen + (max_neffects + 1) * 40;
	char* cp = Color + (max_neffects + 1) * 40;

	for(char i=0; i<40; i++)
	{
		dp[i] = MenuRow[i];
		cp[i] = VCOL_LT_BLUE;
	}
    showdrive();
}

void hires_draw_start(void);

void showfxs_row(char n)
{
	char * dp = Screen + 40 * (n + 1);
	char * cp = Color + 40 * (n + 1);

	for(char i=0; i<40; i++)
	{
		dp[i] = SidRow[i];
		cp[i] = VCOL_DARK_GREY;
	}

	if (n < neffects)
	{
		char fs[6];
		dp[0] = n < 10 ? '0' + n : S'a' + n - 10;
		cp[0] = VCOL_YELLOW;

		SIDFX	&	s = effects[n];
		if (s.ctrl & SID_CTRL_TRI)   cp[2] = VCOL_YELLOW;
		if (s.ctrl & SID_CTRL_SAW)   cp[3] = VCOL_YELLOW;
		if (s.ctrl & SID_CTRL_RECT)  cp[4] = VCOL_YELLOW;
		if (s.ctrl & SID_CTRL_NOISE) cp[5] = VCOL_YELLOW;
		if (s.ctrl & SID_CTRL_GATE)  cp[6] = VCOL_YELLOW;

		uto5digit(s.freq, fs);
		for(char i=0; i<5; i++)
		{
			dp[8 + i] = fs[i];
			cp[8 + i] = VCOL_LT_GREY;
		}

		uto5digit(s.pwm, fs);
		for(char i=0; i<4; i++)
		{
			dp[14 + i] = fs[i + 1];
			cp[14 + i] = VCOL_LT_GREY;
		}

		if (s.dfreq < 0)
		{
			uto5digit(- s.dfreq, fs);
			for(char i=0; i<5; i++)
			{
				dp[24 + i] = fs[i];
				cp[24 + i] = VCOL_ORANGE;
			}				
		}
		else
		{
			uto5digit(s.dfreq, fs);
			for(char i=0; i<5; i++)
			{
				dp[24 + i] = fs[i];
				cp[24 + i] = VCOL_LT_GREY;
			}								
		}

		if (s.dpwm < 0)
		{
			uto5digit(- s.dpwm, fs);
			for(char i=0; i<4; i++)
			{
				dp[30 + i] = fs[i + 1];
				cp[30 + i] = VCOL_ORANGE;
			}				
		}
		else
		{
			uto5digit(s.dpwm, fs);
			for(char i=0; i<4; i++)
			{
				dp[30 + i] = fs[i + 1];
				cp[30 + i] = VCOL_LT_GREY;
			}								
		}

		dp[19] = HexDigit[s.attdec >> 4];   cp[19] = VCOL_YELLOW;
		dp[20] = HexDigit[s.attdec & 0x0f]; cp[20] = VCOL_YELLOW;
		dp[21] = HexDigit[s.susrel >> 4];   cp[21] = VCOL_YELLOW;
		dp[22] = HexDigit[s.susrel & 0x0f]; cp[22] = VCOL_YELLOW;

		uto5digit(s.time1, fs);
		for(char i=0; i<2; i++)
		{
			dp[35 + i] = fs[i + 3];
			cp[35 + i] = VCOL_LT_GREY;
		}
		uto5digit(s.time0, fs);
		for(char i=0; i<2; i++)
		{
			dp[38 + i] = fs[i + 3];
			cp[38 + i] = VCOL_LT_GREY;
		}


	}		
}

void showfxs(void)
{
	char * dp = Screen;
	char * cp = Color;

	for(char i=0; i<40; i++)
	{
		dp[i] = SidHead[i];
		cp[i] = VCOL_LT_BLUE;
	}

	for(char i=0; i<max_neffects; i++)
		showfxs_row(i);
}

char kscan_digits[] = {
	KSCAN_0,
	KSCAN_1,
	KSCAN_2,
	KSCAN_3,
	KSCAN_4,
	KSCAN_5,
	KSCAN_6,
	KSCAN_7,
	KSCAN_8,
	KSCAN_9,
	KSCAN_A,
	KSCAN_B,
	KSCAN_C,
	KSCAN_D,
	KSCAN_E,
	KSCAN_F
};

void check_digit(SIDFX & s, char d)
{
	switch (cursorX)
	{
		case 19: s.attdec = (s.attdec & 0x0f) | (d << 4); break;
		case 20: s.attdec = (s.attdec & 0xf0) | d; break;
		case 21: s.susrel = (s.susrel & 0x0f) | (d << 4); break;
		case 22: s.susrel = (s.susrel & 0xf0) | d; break;
	}

	if (d < 10)
	{
		switch (cursorX)
		{
		case 8:  s.freq = s.freq % 10000 + d * 10000; cursorX++; break;
		case 9:  s.freq = s.freq % 1000 + (s.freq / 10000) * 10000 + d * 1000; cursorX++; break;
		case 10: s.freq = s.freq % 100 + (s.freq / 1000) * 1000 + d * 100; cursorX++; break;
		case 11: s.freq = s.freq % 10 + (s.freq / 100) * 100 + d * 10; cursorX++; break;
		case 12: s.freq = (s.freq / 10 * 10) + d; break;

		case 14: s.pwm = s.pwm % 1000 + (s.pwm / 10000) * 10000 + d * 1000; cursorX++; break;
		case 15: s.pwm = s.pwm % 100 + (s.pwm / 1000) * 1000 + d * 100; cursorX++; break;
		case 16: s.pwm = s.pwm % 10 + (s.pwm / 100) * 100 + d * 10; cursorX++; break;
		case 17: s.pwm = (s.pwm / 10 * 10) + d; break;

		case 35: s.time1 = s.time1 % 10 + d * 10; cursorX++; break;
		case 36: s.time1 = (s.time1 / 10 * 10) + d; break;

		case 38: s.time0 = s.time0 % 10 + d * 10; cursorX++; break;
		case 39: s.time0 = (s.time0 / 10 * 10) + d; break;
		}
	}
}

void edit_filename(char * fn)
{
	char * dp = Screen + (max_neffects + 1) * 40;
	char i = 0;
	while (dp[21 + i] != S'.' && dp[21 + i] != S']')
	{
		char ch = dp[21 + i];
		if (ch >= '0' && ch <= '9' || ch == '-')
			fn[i] = ch;
		else if (ch >= S'A' && ch <= S'Z')
			fn[i] = ch + p'a' - S'A';
		i++;
	}
	fn[i] = 0;
}

void show_msg(const char* msg, bool petscii = false)
{
	save_menu();
	char* sp      = Screen + (max_neffects + 1) * 40;
	char* cp      = Color + (max_neffects + 1) * 40;
	bool  msg_end = false;
	for (char i = 0; i < 40; i++)
	{
		char ch = msg[i];
		if (!ch)
		{
			msg_end = true;
		}
		if (petscii && ch >= 64) ch -= 64;
		sp[i] = msg_end ? S' ' : ch;
		cp[i] = VCOL_RED;
	}
	msg_cnt = 100; // restore menu after 2secs
}

char filenum = 2;
char filechannel = 2;

char drive_status[40];
void read_drive_status(void)
{
    memset(drive_status, 0, 40);
	krnio_setnam("");
	krnio_open(15, drive, 15);
    char n = krnio_read(15, drive_status, 40);
    // trim off trailing CR
    if (n > 0 && drive_status[n-1] == 13) drive_status[n-1] = 0;
    krnio_close(15);
}

void edit_load(void)
{
	rirq_stop();
	vic.intr_enable = 0;
	spr_show(0, false);
	spr_show(1, false);
	spr_show(2, false);
        
#ifdef OSFXEDIT_USE_NMI
	cia2.icr = 0b00000001; // disable NMI
#endif
	bool ok = false;

	char fname[24];

	strcpy(fname, "@0:");
	edit_filename(fname);
	strcat(fname, ",P,R");

	krnio_setnam(fname);
	if (krnio_open(filenum, drive, filechannel))
	{

		// Magic code and save file version
		char v = krnio_getch(filenum);
        auto status = krnio_status();
		if (status == KRNIO_OK)
		{
			if (v <= 0xb3)
			{
				neffects = krnio_getch(filenum);
				krnio_read(filenum, (char*)effects, sizeof(SIDFX) * neffects);
				ok = true;
			}
			else
			{
				show_msg(S"incorrect file version");
			}
		}
		else if (status & KRNIO_EOF)
        {
			show_msg(S"could not read from file");
		}
		else
		{
			read_drive_status();
			show_msg(drive_status, true);
		}
		krnio_close(filenum);
	}
	else
	{
		show_msg(S"drive does not exist");
	}

#ifdef OSFXEDIT_USE_NMI
	cia2.icr = 0b10000001; // enable NMI
#endif
	spr_show(0, true);
	spr_show(1, true);
	spr_show(2, true);
	vic.intr_enable = 1;
	rirq_start();
}

void edit_save(void)
{
	rirq_stop();
	vic.intr_enable = 0;
	spr_show(0, false);
	spr_show(1, false);
	spr_show(2, false);

#ifdef OSFXEDIT_USE_NMI
	cia2.icr = 0b00000001; // disable NMI
#endif

	bool ok = false;

	char fname[24];

	strcpy(fname, "@0:");
	edit_filename(fname + 3);
	strcat(fname, ",P,W");

	krnio_setnam(fname);
	if (krnio_open(filenum, drive, filechannel))
	{
		// Magic code and save file version
		krnio_putch(2, 0xb3);
		if (krnio_status() == KRNIO_OK)
		{
			krnio_putch(filenum, neffects);
			krnio_write(filenum, (char*)effects, sizeof(SIDFX) * neffects);
			if (krnio_status() == KRNIO_OK) ok = true;
		}

		krnio_close(filenum);
		if (ok)
		{
			strcpy(fname, "@0:");
			edit_filename(fname + 3);
                        strcat(fname, p".c" ",P,W");

			krnio_setnam(fname);
			krnio_open(filenum, drive, filechannel);
			edit_filename(fname);

			char buffer[200];
			int  len = sprintf(buffer, "static const SIDFX SFX_%s[] = {\n", fname);
			krnio_write(filenum, buffer, len);
			if (krnio_status() == KRNIO_OK) {

				for (char i = 0; i < neffects; i++)
				{
					const SIDFX& s(effects[i]);
					len = sprintf(
					    buffer,
					    "\t{%u, %u, 0x%02x, 0x%02x, 0x%02x, %d, %d, %d, %d, 0},\n",
					    s.freq, s.pwm, s.ctrl, s.attdec, s.susrel, s.dfreq, s.dpwm,
					    s.time1, s.time0);
					krnio_write(filenum, buffer, len);
				}
				len = sprintf(buffer, "};\n");
				krnio_write(filenum, buffer, len);
			}
			else
			{
				read_drive_status();
				show_msg(drive_status, true);
			}
			krnio_close(filenum);
		}
		else
		{
			read_drive_status();
			show_msg(drive_status, true);
		}
	}
	else
	{
		show_msg(S"drive does not exist");
	}

#ifdef OSFXEDIT_USE_NMI
	cia2.icr = 0b10000001; // enable NMI
#endif
	spr_show(0, true);
	spr_show(1, true);
	spr_show(2, true);
	vic.intr_enable = 1;
	rirq_start();
}

void edit_new(void)
{
	neffects = 1;
	effects[0] = basefx;
}

char * menup = Screen + (max_neffects + 1) * 40;

void edit_menu(char k)
{

	switch(k)
	{
	case KSCAN_CSR_DOWN | KSCAN_QUAL_SHIFT:
		if (neffects < max_neffects)
			cursorY = neffects;
		else
			cursorY = 9;
		break;
	case KSCAN_CSR_RIGHT:
		if (cursorX < 15)
			cursorX += 5;
		else if (cursorX == 15)
			cursorX = 21;
		else if (cursorX < 34 && menup[cursorX] != '.')
			cursorX++;
		break;
	case KSCAN_CSR_RIGHT | KSCAN_QUAL_SHIFT:
		if (cursorX > 21)
			cursorX --;
		else if (cursorX == 21)
			cursorX = 15;
		else if (cursorX > 0)
			cursorX -= 5;
		break;
	case KSCAN_RETURN:
		switch (cursorX)
		{
		case 0:
			edit_load();
			showfxs();
			hires_draw_start();
                        cursorY = neffects;
			break;
		case 5:
			edit_save();
                        cursorY = neffects;
			cursorX = 0;
			break;
		case 10:
			edit_new();
			showfxs();
			hires_draw_start();
                        cursorY = neffects;
			cursorX = 0;
			break;
		default:
			cursorX = 0;
			break;
		}
		break;
	case KSCAN_PLUS:
	case KSCAN_DOT:
	case KSCAN_EQUAL:
        if (cursorX == 15 && drive < 11)
        {
          drive++;
          showdrive();
        }
		break;
	case KSCAN_MINUS:
	case KSCAN_COMMA:
        if (cursorX == 15 && drive > 8)
        {
          drive--;
          showdrive();
        }
		break;
	case KSCAN_DEL:
		if (cursorX > 21)
		{
			cursorX--;
			for(char i=cursorX; i<34; i++)
				menup[i] = menup[i + 1];
			menup[34] = '.';
		}
		break;
	case KSCAN_HOME:
		cursorX = 0;
		cursorY = 0;
		break;
	default:
		if (cursorX >= 21)
		{
			char ch = keyb_codes[k];
			if (ch >= '0' && ch <= '9' || ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z' || ch == '-')
			{
				if (ch >= 'a' && ch <= 'z')
					ch -= 'a' - S'A';
				else if (ch >= 'A' && ch <= 'Z')
					ch -= 'A' - S'A';

				for(char i=33; i>=cursorX; i--)
					menup[i + 1] = menup[i];
				menup[cursorX] = ch;
				if (cursorX < 38)
					cursorX++;
			}
		}
		break;
	}
}

void edit_effects(char k)
{
	bool	restart = false;
	bool	redraw = false;
	bool	redraw_all = false;

	SIDFX	&	s = effects[cursorY];

	switch (k)
	{
	case KSCAN_RETURN:
		cursorX = 0;
		if (cursorY < 9)
			cursorY++;
		break;
	case KSCAN_SPACE:
		restart = true;
		break;
	case KSCAN_CSR_DOWN:
		if (cursorY < neffects)
			cursorY++;
		else
			cursorY = max_neffects;
		break;
	case KSCAN_CSR_DOWN | KSCAN_QUAL_SHIFT:
		if (cursorY > 0)
			cursorY--;
		break;
	case KSCAN_CSR_RIGHT:
		if (cursorX < 39)
			cursorX++;
		break;
	case KSCAN_CSR_RIGHT | KSCAN_QUAL_SHIFT:
		if (cursorX > 0)
			cursorX--;
		break;
	case KSCAN_HOME:
		cursorX = 0;
		break;
	case KSCAN_PLUS:
	case KSCAN_DOT:
	case KSCAN_EQUAL:
		switch (cursorX)
		{
		case 0:
			 if (neffects < max_neffects)
			 {
			 	if (cursorY == neffects)
			 		effects[neffects] = basefx;
			 	else
			 	{
                                        for(char i=neffects; i>cursorY; i--) {
				 		effects[i] = effects[i - 1];
                                        }
                                        redraw_all = true;
			 	}

			 	neffects++; 
			 } 
			 break;

		case 2: s.ctrl |= SID_CTRL_TRI; s.ctrl &= ~SID_CTRL_NOISE; break;
		case 3: s.ctrl |= SID_CTRL_SAW; s.ctrl &= ~SID_CTRL_NOISE;break;
		case 4: s.ctrl |= SID_CTRL_RECT; s.ctrl &= ~SID_CTRL_NOISE;break;
		case 5: s.ctrl |= SID_CTRL_NOISE; s.ctrl &= ~(SID_CTRL_TRI | SID_CTRL_SAW | SID_CTRL_RECT); break;
		case 6: s.ctrl |= SID_CTRL_GATE; break;

		case  8: if (s.freq < 55536) s.freq += 10000; break;
		case  9: if (s.freq < 64536) s.freq +=  1000; break;
		case 10: if (s.freq < 65436) s.freq +=   100; break;
		case 11: if (s.freq < 65526) s.freq +=    10; break;
		case 12: if (s.freq < 65535) s.freq +=     1; break;

		case 14: if (s.pwm < 3096) s.pwm +=  1000; break;
		case 15: if (s.pwm < 3996) s.pwm +=   100; break;
		case 16: if (s.pwm < 4086) s.pwm +=    10; break;
		case 17: if (s.pwm < 4085) s.pwm +=     1; break;

		case 19: if ((s.attdec & 0xf0) < 0xf0) s.attdec += 0x10; break;
		case 20: if ((s.attdec & 0x0f) < 0x0f) s.attdec += 0x01; break;
		case 21: if ((s.susrel & 0xf0) < 0xf0) s.susrel += 0x10; break;
		case 22: if ((s.susrel & 0x0f) < 0x0f) s.susrel += 0x01; break;

		case 24: if (s.dfreq < 22768) s.dfreq += 10000; break;
		case 25: if (s.dfreq < 31768) s.dfreq +=  1000; break;
		case 26: if (s.dfreq < 32668) s.dfreq +=   100; break;
		case 27: if (s.dfreq < 32758) s.dfreq +=    10; break;
		case 28: if (s.dfreq < 32767) s.dfreq +=     1; break;

		case 30: if (s.dpwm < 3096) s.dpwm +=  1000; break;
		case 31: if (s.dpwm < 3996) s.dpwm +=   100; break;
		case 32: if (s.dpwm < 4086) s.dpwm +=    10; break;
		case 33: if (s.dpwm < 4095) s.dpwm +=     1; break;

		case 35: if (s.time1 < 90) s.time1 += 10; break;
		case 36: if (s.time1 < 99) s.time1 +=  1; break;
		case 38: if (s.time0 < 90) s.time0 += 10; break;
		case 39: if (s.time0 < 99) s.time0 +=  1; break;

		}
		restart = true;
		redraw = true;
		break;
	case KSCAN_MINUS:
	case KSCAN_COMMA:
		switch (cursorX)
		{
		case 0: 
			if (neffects > 1)
			{
				neffects--;
				for(char i=cursorY; i<neffects; i++)
					effects[i] = effects[i + 1];						
                                redraw_all = true;
			}
			break;

		case 2: s.ctrl &= ~SID_CTRL_TRI; break;
		case 3: s.ctrl &= ~SID_CTRL_SAW; break;
		case 4: s.ctrl &= ~SID_CTRL_RECT; break;
		case 5: s.ctrl &= ~SID_CTRL_NOISE; break;
		case 6: s.ctrl &= ~SID_CTRL_GATE; break;

		case  8: if (s.freq >= 10000) s.freq -= 10000; break;
		case  9: if (s.freq >=  1000) s.freq -=  1000; break;
		case 10: if (s.freq >=   100) s.freq -=   100; break;
		case 11: if (s.freq >=    10) s.freq -=    10; break;
		case 12: if (s.freq >=     1) s.freq -=     1; break;

		case 14: if (s.pwm >=  1000) s.pwm -=  1000; break;
		case 15: if (s.pwm >=   100) s.pwm -=   100; break;
		case 16: if (s.pwm >=    10) s.pwm -=    10; break;
		case 17: if (s.pwm >=     1) s.pwm -=     1; break;

		case 19: if ((s.attdec & 0xf0) > 0x00) s.attdec -= 0x10; break;
		case 20: if ((s.attdec & 0x0f) > 0x00) s.attdec -= 0x01; break;
		case 21: if ((s.susrel & 0xf0) > 0x00) s.susrel -= 0x10; break;
		case 22: if ((s.susrel & 0x0f) > 0x00) s.susrel -= 0x01; break;

		case 24: if (s.dfreq > -22768) s.dfreq -= 10000; break;
		case 25: if (s.dfreq > -31768) s.dfreq -=  1000; break;
		case 26: if (s.dfreq > -32668) s.dfreq -=   100; break;
		case 27: if (s.dfreq > -32758) s.dfreq -=    10; break;
		case 28: if (s.dfreq > -32767) s.dfreq -=     1; break;

		case 30: if (s.dpwm > -3096) s.dpwm -=  1000; break;
		case 31: if (s.dpwm > -3996) s.dpwm -=   100; break;
		case 32: if (s.dpwm > -4086) s.dpwm -=    10; break;
		case 33: if (s.dpwm > -4095) s.dpwm -=     1; break;

		case 35: if (s.time1 >= 10) s.time1 -= 10; break;
		case 36: if (s.time1 >   0) s.time1 -=  1; break;
		case 38: if (s.time0 >= 10) s.time0 -= 10; break;
		case 39: if (s.time0 >   0) s.time0 -=  1; break;
		}
		restart = true;
		redraw = true;
		break;

	default:
		char i = 0;
		while (i < 16 && k != kscan_digits[i])
			i++;
		if (i < 16)
		{
			check_digit(s, i); 
			restart = true;
			redraw = true;
		}
		break;

	}

	if (restart)
	{
		sidfx_stop(voice);
		sidfx_play(voice, effects, neffects);
		irq_cnt = 0;
	}

	if (redraw_all)
	{
		showfxs();
		hires_draw_start();
	}
        else if (redraw)
	{
		showfxs_row(cursorY);		
		hires_draw_start();
	}

}

enum Phase
{
	PHASE_RELEASE,
	PHASE_OFF,
	PHASE_ATTACK,
	PHASE_DECAY
};

enum SIDFXState
{
	SIDFX_IDLE,
	SIDFX_RESET_0,
	SIDFX_READY,
	SIDFX_PLAY,
	SIDFX_WAIT
};

struct VirtualSID
{	
	Phase			phase;
	char			ctrl;
	char			attdec, susrel;
	unsigned		adsr, freq, pwm;
	char			tick, delay, pos;
	SIDFXState		state;

}	vsid;

// Maximum value and per frame step for ADSR emulation
static const unsigned AMAX	= 32 * 256 - 1;
#ifdef OSFXEDIT_USE_NMI
static const unsigned TSTEP = nmi_cycles / 1000; // ms/frame
#else
static const unsigned TSTEP = 20; // ms/frame
#endif
static const unsigned ASTEP = (unsigned long)AMAX * TSTEP / 4;

// ADSR constants
static const unsigned AttackStep[16] = {
	ASTEP / 2,   ASTEP / 8,  ASTEP / 16, ASTEP / 24,
	ASTEP / 38,   ASTEP / 56,  ASTEP / 68, ASTEP / 80,
	ASTEP / 100,   ASTEP / 250,  ASTEP / 500, ASTEP / 800,
	ASTEP / 1000,   ASTEP / 3000,  ASTEP / 5000, ASTEP / 8000
};

static const unsigned DecayStep[16] = {
	ASTEP / 6,   ASTEP / 24,  ASTEP / 48, ASTEP / 72,
	ASTEP / 114,   ASTEP / 168,  ASTEP / 204, ASTEP / 240,
	ASTEP / 300,   ASTEP / 750,  ASTEP / 1500, ASTEP / 2400,
	ASTEP / 3000,   ASTEP / 9000,  ASTEP / 15000, ASTEP / 24000
};

static const char Count2Level[256] = {
	#for (i, 256) exp(i / 54.0) / exp(255.0 / 54.0) * 31.0,
};

static const char Sustain2Count[16] = {
	#for (i, 16) log(i * exp(255.0 / 54.0) / 15.0) * 54.0,
};


void vsid_advance(void)
{
	if (vsid.ctrl & SID_CTRL_GATE)
	{
		if (vsid.phase < PHASE_ATTACK)
		{
			vsid.phase = PHASE_ATTACK;
			vsid.adsr = 0;
		}
	}
	else
	{
		if (vsid.phase >= PHASE_ATTACK)
			vsid.phase = PHASE_RELEASE;
	}

	switch (vsid.phase)
	{
	case PHASE_ATTACK:
		// Increase channel power during attack phase
		vsid.adsr += AttackStep[vsid.attdec >> 4];
		if (vsid.adsr >= AMAX)
		{
			// Switch to decay phase next
			vsid.adsr = AMAX;
			vsid.phase = PHASE_DECAY;
		}
		break;
	case PHASE_DECAY:
		{
			// Decrease channel power during decay phase
			unsigned sus = Sustain2Count[vsid.susrel >> 4] << 5;
			unsigned dec = DecayStep[vsid.attdec & 0x0f];

			if (vsid.adsr > sus + dec)
				vsid.adsr -= dec;
			else if (vsid.adsr > sus)
				vsid.adsr = sus;
		}
		break;
	case PHASE_RELEASE:
		{
			// Decrease channel power during release phase
			unsigned dec = DecayStep[vsid.susrel & 0x0f];

			if (vsid.adsr > dec)
				vsid.adsr -= dec;
			else
			{
				// Switch to off when energy reaches flat
				vsid.adsr = 0;
				vsid.phase = PHASE_OFF;
			}
		}
		break;
	}


}

void hires_bar(char * dp, const char * ady)
{
	char c = 0;
	for(char i=0; i<4; i++)
	{		
		for(char j=0; j<8; j+=2)
		{
			c |= ady[j];
			dp[j] = c;
			c |= ady[j + 1];
			dp[j + 1] = c ^ 0x22;
		}
		dp += 320;
		ady += 8;
	}
}

static const char binlog32[256] = {
#for(i, 256)	(int)(log(i) / log(2) * 4),
};


void hires_draw_start(void)
{
	vsid.phase = PHASE_OFF;
	vsid.ctrl = 0;
	vsid.state = SIDFX_READY;
	vsid.delay = 1;
	vsid.tick = 0;
	vsid.pos = 0;
}

void hires_draw_tick(void)
{
	char ady[32], fry[32];

	if (vsid.tick < 40)
	{
		for(char i=0; i<32; i++)
			ady[i] = fry[i] = 0;

		for(char n=0; n<2; n++)
		{
			const SIDFX	*	com = effects + vsid.pos;
			vsid.delay--;
			if (vsid.delay)
			{
				if (com->dfreq)
					vsid.freq += com->dfreq;
				if (com->dpwm)
					vsid.pwm += com->dpwm;				
			}
			while (!vsid.delay)
			{
				switch (vsid.state)
				{
				case SIDFX_IDLE:
					vsid.delay = 1;
					break;
				case SIDFX_RESET_0:
					vsid.ctrl = 0;
					vsid.attdec = 0;
					vsid.susrel = 0;
					vsid.state = SIDFX_READY;
					vsid.delay = 1;
					break;
				case SIDFX_READY:
					if (vsid.pos < neffects)
					{
						vsid.freq = com->freq;
						vsid.pwm = com->pwm;
						vsid.attdec = com->attdec;
						vsid.susrel = com->susrel;
						vsid.ctrl = com->ctrl;

						if (com->ctrl & SID_CTRL_GATE)
						{
							vsid.delay = com->time1;
							vsid.state = SIDFX_PLAY;
						}
						else
						{
							vsid.delay = com->time0;
							vsid.state = SIDFX_WAIT;
						}
					}
					else
						vsid.state = SIDFX_IDLE;
					break;
				case SIDFX_PLAY:
					if (com->time0)
					{
						vsid.ctrl = com->ctrl & ~SID_CTRL_GATE;
						vsid.delay = com->time0 - 1;
						vsid.state = SIDFX_WAIT;
					}
					else
					{
						vsid.pos++;
						if (vsid.pos < neffects)
						{
							char sr = com->susrel & 0xf0;
							com++;
							if ((com->attdec & 0xef) == 0 && (com->ctrl & SID_CTRL_GATE) && (com->susrel & 0xf0) > sr)
								vsid.phase = PHASE_RELEASE;
							vsid.state = SIDFX_READY;
						}
						else
							vsid.state = SIDFX_RESET_0;
					}
					break;
				case SIDFX_WAIT:
					vsid.pos++;
					if (vsid.pos < neffects)
					{
						com++;
						if (com->ctrl & SID_CTRL_GATE)
							vsid.state = SIDFX_RESET_0;
						else
							vsid.state = SIDFX_READY;
					}
					else
						vsid.state = SIDFX_RESET_0;
					break;
				}
			}


			for(char i=0; i<4; i++)
			{
				char j = i + 4 * n;
				vsid_advance();
				if (vsid.phase == PHASE_ATTACK)
					ady[31 - (vsid.adsr >> 8)] |= 128 >> j;
				else
					ady[31 - Count2Level[vsid.adsr >> 5]] |= 128 >> j;
				fry[31 - binlog32[vsid.freq >> 8]] |= 128 >> j;
			}
		}

		char * dp = Hires + 320 * (max_neffects + 2) + 8 * vsid.tick;
		hires_bar(dp, ady);
		dp += 320 * 4;
		hires_bar(dp, fry);	
		vsid.tick++;
	}
}

int main(void)
{
	__asm {sei}

	cia_init();

	mmap_set(MMAP_CHAR_ROM);
	memcpy(Hires, ROMFont, 0x0800); // use redundant 1st half of hires to store font
	mmap_set(MMAP_NO_BASIC);
	memcpy((char*)0xe000, (char*)0xe000, 0x2000); // copy ROM to RAM
	mmap_set(MMAP_NO_ROM);

	memset(Sprites, 0, 256);
	for(char i=0; i<9; i++)
		Sprites[0 * 64 + 3 * i] = 0xff;
	Sprites[1 * 64 + 3 * 8] = 0xff;
	for(char i=0; i<21; i++)
		Sprites[2 * 64 + 3 * i] = 0xf0;

	spr_init(Screen);

	vic_setmode(VICM_TEXT, Screen, Hires);

#ifdef OSFXEDIT_USE_NMI
	*(void**)0xfffa = nmi_isr_stub;
	vic_waitLine(nmi_start_rasterline); // start in consistent place to avoid flicker at hires transition
	cia2.ta	 = nmi_cycles;
	cia2.icr = 0b10000001;
	cia2.cra = 0b00010001;
#endif

	sidfx_init();

	rirq_init_kernal();

	rirq_build(&rirq_isr, 2);
	rirq_write(&rirq_isr, 0, &vic.ctrl1, VIC_CTRL1_DEN | VIC_CTRL1_RSEL | 3);
	rirq_call(&rirq_isr, 1, isr);
	rirq_set(0, 250, &rirq_isr);

	rirq_build(&rirq_mark0, 1);
	rirq_write(&rirq_mark0, 0, &vic.color_back, VCOL_BLUE);

	rirq_build(&rirq_mark1, 1);
	rirq_write(&rirq_mark1, 0, &vic.color_back, VCOL_BLACK);

	rirq_build(&rirq_bitmap, 2);
	rirq_delay(&rirq_bitmap, 10);
	rirq_write(&rirq_bitmap, 1, &vic.ctrl1, VIC_CTRL1_DEN | VIC_CTRL1_RSEL | VIC_CTRL1_BMM | 3);
	rirq_set(3, 49 + 8 * (max_neffects + 2), &rirq_bitmap);

	rirq_sort();
	rirq_start();

	sid.fmodevol = 15;

	vic.color_border = VCOL_BLACK;
	vic.color_back = VCOL_BLACK;

	memset(Screen, 0x10, 1000);

	effects[0] = basefx;
	neffects = 1;

	showfxs();		
	showmenu();
	hires_draw_start();

	memset(Hires + (max_neffects + 2) * 320, 0, 13 * 320);
	memset(Screen + (max_neffects + 2) * 40, 0x70, 160);
	memset(Screen + (max_neffects + 2 + 4) * 40, 0xe0, 160);

	spr_set(0, true, 0, 0, sprite_img_base + 0, VCOL_WHITE, false, false, false);
	spr_set(1, true, 0, 0, sprite_img_base + 2, VCOL_BLUE, false, false, true);
	spr_set(2, true, 0, 0, sprite_img_base + 2, VCOL_BLUE, false, false, true);

	vic.spr_priority = 0x07;

	bool	markset = false;
	for(;;)
	{
		char * curp = Screen + 40 + 40 * cursorY + cursorX;
		char * curc = Color + 40 + 40 * cursorY + cursorX;

		if (cursorY < max_neffects || cursorX >= 20)
		{
			spr_move(0, 24 + 8 * cursorX, 49 + 8 + 8 * cursorY);
			spr_image(0, sprite_img_base + ((csr_cnt >> 4) & 1));
		}
		else
		{
			spr_move(0, 0, 0);
			for(char i=0; i<4; i++)
				curc[i] = VCOL_YELLOW;
		}

		// vic.color_border = VCOL_ORANGE;
		hires_draw_tick();
		hires_draw_tick();
		hires_draw_tick();
		// vic.color_border = VCOL_BLACK;

		vic_waitBottom();
		if (sidfx_idle(voice))
		{
			spr_move(1, 0, 0);
			spr_move(2, 0, 0);

			if (markset)
			{
				rirq_clear(1); rirq_clear(2);
				rirq_sort();
				markset = false;
			}
		}
		else
		{
			if (irq_cnt < 100)
			{
				spr_move(1, 24 + 4 * irq_cnt, (max_neffects + 2) * 8 + 49);
				spr_move(2, 24 + 4 * irq_cnt, (max_neffects + 2 + 3) * 8 + 49);
			}
			else
			{
				spr_move(1, 0, 0);
				spr_move(2, 0, 0);
			}

			char sx = (neffects - sidfx_cnt(voice)) * 8 + 49;
			if (!markset)
			{
				rirq_set(1, sx, &rirq_mark0); 
				rirq_set(2, sx + 8, &rirq_mark1); 
				markset = true;
			}
			else
			{
				rirq_move(1, sx); 
				rirq_move(2, sx + 8); 
			}
			rirq_sort();
		}

		if (cursorY < max_neffects || cursorX >= 20)
			;
		else
		{
			for(char i=0; i<4; i++)
				curc[i] = VCOL_LT_BLUE;
		}

		if (keyb_queue & KSCAN_QUAL_DOWN)
		{
			csr_cnt = 16;

			char k = keyb_queue & KSCAN_QUAL_MASK;
			keyb_queue = 0;

			if (cursorY < max_neffects)
			{
				edit_effects(k);
				if (cursorY == max_neffects)
				{
					if (cursorX < 20)
						cursorX = cursorX / 5 * 5;
					else
					{
						while (cursorX > 34 || menup[cursorX - 1] == '.')
							cursorX--;
					}
				}
			}
			else
			{
				edit_menu(k);
			}
		}
	}
}
