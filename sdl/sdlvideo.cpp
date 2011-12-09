/***********************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.

  See CREDITS file to find the copyright owners of this file.

  SDL Audio/Video glue code (mostly borrowed from snes9x & drnoksnes)
  (c) Copyright 2011         Makoto Sugano (makoto.sugano@gmail.com)

  Snes9x homepage: http://www.snes9x.com/

  Permission to use, copy, modify and/or distribute Snes9x in both binary
  and source form, for non-commercial purposes, is hereby granted without
  fee, providing that this license information and copyright notice appear
  with all copies and any derived work.

  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software or it's derivatives.

  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes,
  but is not limited to, charging money for Snes9x or software derived from
  Snes9x, including Snes9x or derivatives in commercial game bundles, and/or
  using Snes9x as a promotion for your commercial product.

  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.

  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
 ***********************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "controls.h"
#include "movie.h"
#include "logger.h"
#include "conffile.h"
#include "blit.h"
#include "display.h"

#define HAVE_SDL 1

#ifdef HAVE_SDL

#include <SDL/SDL.h>
#include <iostream>
SDL_Surface *screen;
using namespace std;
std::map <string, int> name_sdlkeysym;

#endif

struct GUIData
{
	uint8			*snes_buffer;
#ifndef HAVE_SDL
	uint8			*filter_buffer;
#endif
	uint8			*blit_screen;
	uint32			blit_screen_pitch;
	int				video_mode;
	int				mouse_x;
	int				mouse_y;
        bool8                   fullscreen;
};

static struct GUIData	GUI;

typedef	std::pair<std::string, std::string>	strpair_t;
extern	std::vector<strpair_t>				keymaps;

typedef	void (* Blitter) (uint8 *, int, uint8 *, int, int, int);

#ifdef __linux
// Select seems to be broken in 2.x.x kernels - if a signal interrupts a
// select system call with a zero timeout, the select call is restarted but
// with an infinite timeout! The call will block until data arrives on the
// selected fd(s).
//
// The workaround is to stop the X library calling select in the first
// place! Replace XPending - which polls for data from the X server using
// select - with an ioctl call to poll for data and then only call the blocking
// XNextEvent if data is waiting.
#define SELECT_BROKEN_FOR_SIGNALS
#endif

enum
{
	VIDEOMODE_BLOCKY = 1,
	VIDEOMODE_TV,
	VIDEOMODE_SMOOTH,
	VIDEOMODE_SUPEREAGLE,
	VIDEOMODE_2XSAI,
	VIDEOMODE_SUPER2XSAI,
	VIDEOMODE_EPX,
	VIDEOMODE_HQ2X
};

static void SetupImage (void);
static void TakedownImage (void);
static void Repaint (bool8);

void S9xExtraDisplayUsage (void)
{
	S9xMessage(S9X_INFO, S9X_USAGE, "-fullscreen                     fullscreen mode (without scaling)");
	S9xMessage(S9X_INFO, S9X_USAGE, "");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v1                             Video mode: Blocky (default)");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v2                             Video mode: TV");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v3                             Video mode: Smooth");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v4                             Video mode: SuperEagle");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v5                             Video mode: 2xSaI");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v6                             Video mode: Super2xSaI");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v7                             Video mode: EPX");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v8                             Video mode: hq2x");
	S9xMessage(S9X_INFO, S9X_USAGE, "");
}

void S9xParseDisplayArg (char **argv, int &i, int argc)
{
	if (!strncasecmp(argv[i], "-fullscreen", 11))
        {
                GUI.fullscreen = TRUE;
                printf ("Entering fullscreen mode (without scaling).\n");
        }
        else
	if (!strncasecmp(argv[i], "-v", 2))
	{
		switch (argv[i][2])
		{
			case '1':	GUI.video_mode = VIDEOMODE_BLOCKY;		break;
			case '2':	GUI.video_mode = VIDEOMODE_TV;			break;
			case '3':	GUI.video_mode = VIDEOMODE_SMOOTH;		break;
			case '4':	GUI.video_mode = VIDEOMODE_SUPEREAGLE;	break;
			case '5':	GUI.video_mode = VIDEOMODE_2XSAI;		break;
			case '6':	GUI.video_mode = VIDEOMODE_SUPER2XSAI;	break;
			case '7':	GUI.video_mode = VIDEOMODE_EPX;			break;
			case '8':	GUI.video_mode = VIDEOMODE_HQ2X;		break;
		}
	}
	else
		S9xUsage();
}

const char * S9xParseDisplayConfig (ConfigFile &conf, int pass)
{
	if (pass != 1)
		return ("Unix/X11");

	// domaemon: FIXME, just collecting the essentials.
	// domaemon: *) here we define the keymapping.
	if (!conf.GetBool("Unix::ClearAllControls", false))
	{
		keymaps.push_back(strpair_t("K00:SDLK_RIGHT",        "Joypad1 Right"));
		keymaps.push_back(strpair_t("K00:SDLK_LEFT",         "Joypad1 Left"));
		keymaps.push_back(strpair_t("K00:SDLK_DOWN",         "Joypad1 Down"));
		keymaps.push_back(strpair_t("K00:SDLK_UP",           "Joypad1 Up"));
		keymaps.push_back(strpair_t("K00:SDLK_RETURN",       "Joypad1 Start"));
		keymaps.push_back(strpair_t("K00:SDLK_SPACE",        "Joypad1 Select"));
		keymaps.push_back(strpair_t("K00:SDLK_d",            "Joypad1 A"));
		keymaps.push_back(strpair_t("K00:SDLK_c",            "Joypad1 B"));
		keymaps.push_back(strpair_t("K00:SDLK_s",            "Joypad1 X"));
		keymaps.push_back(strpair_t("K00:SDLK_x",            "Joypad1 Y"));
		keymaps.push_back(strpair_t("K00:SDLK_a",            "Joypad1 L"));
		keymaps.push_back(strpair_t("K00:SDLK_z",            "Joypad1 R"));

		// domaemon: *) GetSDLKeyFromName().
		name_sdlkeysym["SDLK_s"] = SDLK_s;
		name_sdlkeysym["SDLK_d"] = SDLK_d;
		name_sdlkeysym["SDLK_x"] = SDLK_x;
		name_sdlkeysym["SDLK_c"] = SDLK_c;
		name_sdlkeysym["SDLK_a"] = SDLK_a;
		name_sdlkeysym["SDLK_z"] = SDLK_z;
		name_sdlkeysym["SDLK_UP"] = SDLK_UP;
		name_sdlkeysym["SDLK_DOWN"] = SDLK_DOWN;
		name_sdlkeysym["SDLK_RIGHT"] = SDLK_RIGHT;
		name_sdlkeysym["SDLK_LEFT"] = SDLK_LEFT;
		name_sdlkeysym["SDLK_RETURN"] = SDLK_RETURN;
		name_sdlkeysym["SDLK_SPACE"] = SDLK_SPACE;
	}

	if (conf.Exists("Unix/X11::VideoMode"))
	{
		GUI.video_mode = conf.GetUInt("Unix/X11::VideoMode", VIDEOMODE_BLOCKY);
		if (GUI.video_mode < 1 || GUI.video_mode > 8)
			GUI.video_mode = VIDEOMODE_BLOCKY;
	}
	else
		GUI.video_mode = VIDEOMODE_BLOCKY;


	return ("Unix/X11");
}

static void FatalError (const char *str)
{
	fprintf(stderr, "%s\n", str);
	S9xExit();
}

void S9xInitDisplay (int argc, char **argv)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("Unable to initialize SDL: %s\n", SDL_GetError());
  }
  
  atexit(SDL_Quit);

  /*
   * domaemon
   *
   * we just go along with RGB565 for now, nothing else..
   */

  S9xSetRenderPixelFormat(RGB565);

	S9xBlitFilterInit();
	S9xBlit2xSaIFilterInit();
	S9xBlitHQ2xFilterInit();


	/*
	 * domaemon
	 *
	 * FIXME: The secreen size should be flexible
	 * FIXME: Check if the SDL screen is really in RGB565 mode. screen->fmt	
	 */	
        if (GUI.fullscreen == TRUE)
        {
                screen = SDL_SetVideoMode(0, 0, 16, SDL_FULLSCREEN);
        } else {
                screen = SDL_SetVideoMode(SNES_WIDTH * 2, SNES_HEIGHT_EXTENDED * 2, 16, 0);
        }

        if (screen == NULL) {
          printf("Unable to set video mode: %s\n", SDL_GetError());
          exit(1);
        }

	/*
	 * domaemon
	 *
	 * buffer allocation, quite important
	 */
	SetupImage();
}

void S9xDeinitDisplay (void)
{
	TakedownImage();

	SDL_Quit();

	S9xBlitFilterDeinit();
	S9xBlit2xSaIFilterDeinit();
	S9xBlitHQ2xFilterDeinit();
}

static void TakedownImage (void)
{
	if (GUI.snes_buffer)
	{
		free(GUI.snes_buffer);
		GUI.snes_buffer = NULL;
	}

	S9xGraphicsDeinit();
}

static void SetupImage (void)
{
	TakedownImage();

	// domaemon: The whole unix code basically assumes output=(original * 2);
	// This way the code can handle the SNES filters, which does the 2X.
	GFX.Pitch = SNES_WIDTH * 2 * 2;
	GUI.snes_buffer = (uint8 *) calloc(GFX.Pitch * ((SNES_HEIGHT_EXTENDED + 4) * 2), 1);
	if (!GUI.snes_buffer)
		FatalError("Failed to allocate GUI.snes_buffer.");

	// domaemon: Add 2 lines before drawing.
	GFX.Screen = (uint16 *) (GUI.snes_buffer + (GFX.Pitch * 2 * 2));

#ifndef HAVE_SDL // domaemon: this is used as the destination buffer in the original unix code.
	GUI.filter_buffer = (uint8 *) calloc((SNES_WIDTH * 2) * 2 * (SNES_HEIGHT_EXTENDED * 2), 1);
	if (!GUI.filter_buffer)
		FatalError("Failed to allocate GUI.filter_buffer.");
#endif

	if (GUI.fullscreen == TRUE)
	{
		int offset_height_pix;
		int offset_width_pix;
		int offset_byte;


		offset_height_pix = (screen->h - (SNES_HEIGHT * 2)) / 2;
		offset_width_pix = (screen->w - (SNES_WIDTH * 2)) / 2;
		
		offset_byte = (screen->w * offset_height_pix + offset_width_pix) * 2;

		GUI.blit_screen       = (uint8 *) screen->pixels + offset_byte;
		GUI.blit_screen_pitch = screen->w * 2;
	}
	else 
	{
		GUI.blit_screen       = (uint8 *) screen->pixels;
		GUI.blit_screen_pitch = SNES_WIDTH * 2 * 2; // window size =(*2); 2 byte pir pixel =(*2)
	}

	S9xGraphicsInit();
}

void S9xPutImage (int width, int height)
{
	static int	prevWidth = 0, prevHeight = 0;
	int			copyWidth, copyHeight;
	Blitter		blitFn = NULL;

	if (GUI.video_mode == VIDEOMODE_BLOCKY || GUI.video_mode == VIDEOMODE_TV || GUI.video_mode == VIDEOMODE_SMOOTH)
		if ((width <= SNES_WIDTH) && ((prevWidth != width) || (prevHeight != height)))
			S9xBlitClearDelta();

	if (width <= SNES_WIDTH)
	{
		if (height > SNES_HEIGHT_EXTENDED)
		{
			copyWidth  = width * 2;
			copyHeight = height;
			blitFn = S9xBlitPixSimple2x1;
		}
		else
		{
			copyWidth  = width  * 2;
			copyHeight = height * 2;

			switch (GUI.video_mode)
			{
				case VIDEOMODE_BLOCKY:		blitFn = S9xBlitPixSimple2x2;		break;
				case VIDEOMODE_TV:			blitFn = S9xBlitPixTV2x2;			break;
				case VIDEOMODE_SMOOTH:		blitFn = S9xBlitPixSmooth2x2;		break;
				case VIDEOMODE_SUPEREAGLE:	blitFn = S9xBlitPixSuperEagle16;	break;
				case VIDEOMODE_2XSAI:		blitFn = S9xBlitPix2xSaI16;			break;
				case VIDEOMODE_SUPER2XSAI:	blitFn = S9xBlitPixSuper2xSaI16;	break;
				case VIDEOMODE_EPX:			blitFn = S9xBlitPixEPX16;			break;
				case VIDEOMODE_HQ2X:		blitFn = S9xBlitPixHQ2x16;			break;
			}
		}
	}
	else
	if (height <= SNES_HEIGHT_EXTENDED)
	{
		copyWidth  = width;
		copyHeight = height * 2;

		switch (GUI.video_mode)
		{
			default:					blitFn = S9xBlitPixSimple1x2;	break;
			case VIDEOMODE_TV:			blitFn = S9xBlitPixTV1x2;		break;
		}
	}
	else
	{
		copyWidth  = width;
		copyHeight = height;
		blitFn = S9xBlitPixSimple1x1;
	}


	// domaemon: this is place where the rendering buffer size should be changed?
	blitFn((uint8 *) GFX.Screen, GFX.Pitch, GUI.blit_screen, GUI.blit_screen_pitch, width, height);

	// domaemon: does the height change on the fly?
	if (height < prevHeight)
	{
		int	p = GUI.blit_screen_pitch >> 2;
		for (int y = SNES_HEIGHT * 2; y < SNES_HEIGHT_EXTENDED * 2; y++)
		{
			uint32	*d = (uint32 *) (GUI.blit_screen + y * GUI.blit_screen_pitch);
			for (int x = 0; x < p; x++)
				*d++ = 0;
		}
	}

	Repaint(TRUE);

	prevWidth  = width;
	prevHeight = height;
}

static void Repaint (bool8 isFrameBoundry)
{
        SDL_Flip(screen);
}

void S9xProcessEvents (bool8 block)
{
	SDL_Event event;
	bool8 quit_state = FALSE;

	while ((block) || (SDL_PollEvent (&event) != 0)) {
		switch (event.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			// domaemon: not sure it's a good idea, but I will reserve the SDLK_q for quit.
			if (event.key.keysym.sym == SDLK_q)
			{
				quit_state = TRUE;
			} else {
				S9xReportButton(event.key.keysym.sym, event.type == SDL_KEYDOWN);
			}
			break;
		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			quit_state = TRUE;
		}
	}

	if (quit_state == TRUE)
	{
		printf ("Quit Event. Bye.\n");
		S9xExit();
	}
}

const char * S9xSelectFilename (const char *def, const char *dir1, const char *ext1, const char *title)
{
	static char	s[PATH_MAX + 1];
	char		buffer[PATH_MAX + 1];

	printf("\n%s (default: %s): ", title, def);
	fflush(stdout);

	if (fgets(buffer, PATH_MAX + 1, stdin))
	{
		char	drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

		char	*p = buffer;
		while (isspace(*p))
			p++;
		if (!*p)
		{
			strncpy(buffer, def, PATH_MAX + 1);
			buffer[PATH_MAX] = 0;
			p = buffer;
		}

		char	*q = strrchr(p, '\n');
		if (q)
			*q = 0;

		_splitpath(p, drive, dir, fname, ext);
		_makepath(s, drive, *dir ? dir : dir1, fname, *ext ? ext : ext1);

		return (s);
	}

	return (NULL);
}

void S9xMessage (int type, int number, const char *message)
{
	const int	max = 36 * 3;
	static char	buffer[max + 1];

	fprintf(stdout, "%s\n", message);
	strncpy(buffer, message, max + 1);
	buffer[max] = 0;
	S9xSetInfoString(buffer);
}

const char * S9xStringInput (const char *message)
{
	static char	buffer[256];

	printf("%s: ", message);
	fflush(stdout);

	if (fgets(buffer, sizeof(buffer) - 2, stdin))
		return (buffer);

	return (NULL);
}

void S9xSetTitle (const char *string)
{
  SDL_WM_SetCaption(string, string);
}

s9xcommand_t S9xGetDisplayCommandT (const char *n)
{
	s9xcommand_t	cmd;

	cmd.type         = S9xBadMapping;
	cmd.multi_press  = 0;
	cmd.button_norpt = 0;
	cmd.port[0]      = 0xff;
	cmd.port[1]      = 0;
	cmd.port[2]      = 0;
	cmd.port[3]      = 0;

	return (cmd);
}

char * S9xGetDisplayCommandName (s9xcommand_t cmd)
{
	return (strdup("None"));
}

void S9xHandleDisplayCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{
	return;
}

// domaemon: 2) here we send the keymapping request to the SNES9X
bool8 S9xMapDisplayInput (const char *n, s9xcommand_t *cmd)
{
	int	i, d;

	if (!isdigit(n[1]) || !isdigit(n[2]) || n[3] != ':')
		goto unrecog;

	d = ((n[1] - '0') * 10 + (n[2] - '0')) << 24;

	switch (n[0])
	{
		case 'K':
		{
		  int key;

			d |= 0x00000000;

			for (i = 4; n[i] != '\0' && n[i] != '+'; i++) ;

			if (n[i] == '\0' || i == 4)
				i = 4;
#if 1
			string keyname (n + i);
			//			cout << keyname << endl;
			//			printf ("%d\n", name_sdlkeysym[keyname]);
			key = name_sdlkeysym[keyname];

			d |= key & 0xff;
#endif

			return (S9xMapButton(key, *cmd, false));
		}

		case 'M':
		{
			char	*c;
			int		j;

			d |= 0x40000000;

			if (!strncmp(n + 4, "Pointer", 7))
			{
				d |= 0x8000;

				if (n[11] == '\0')
					return (S9xMapPointer(d, *cmd, true));

				i = 11;
			}
			else
			if (n[4] == 'B')
				i = 5;
			else
				goto unrecog;

			d |= j = strtol(n + i, &c, 10);

			if ((c != NULL && *c != '\0') || j > 0x7fff)
				goto unrecog;

			if (d & 0x8000)
				return (S9xMapPointer(d, *cmd, true));

			return (S9xMapButton(d, *cmd, false));
		}

		default:
			break;
	}

unrecog:
	char	*err = new char[strlen(n) + 34];

	sprintf(err, "Unrecognized input device name '%s'", n);
	perror(err);
	delete [] err;

	return (false);
}

bool S9xDisplayPollButton (uint32 id, bool *pressed)
{
	return (false);
}

bool S9xDisplayPollAxis (uint32 id, int16 *value)
{
	return (false);
}

bool S9xDisplayPollPointer (uint32 id, int16 *x, int16 *y)
{
	if ((id & 0xc0008000) != 0x40008000)
		return (false);

	int	d = (id >> 24) & 0x3f,
		n = id & 0x7fff;

	if (d != 0 || n != 0)
		return (false);

	*x = GUI.mouse_x;
	*y = GUI.mouse_y;

	return (true);
}

void S9xSetPalette (void)
{
	return;
}
