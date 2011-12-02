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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

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
	bool8			mod1_pressed;
	bool8			no_repeat;
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

#ifndef HAVE_SDL
static int ErrorHandler (Display *, XErrorEvent *);
static bool8 CheckForPendingXEvents (Display *);
#endif

static void SetupImage (void);
static void TakedownImage (void);
static void Repaint (bool8);

void S9xExtraDisplayUsage (void)
{
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

	if (!conf.GetBool("Unix::ClearAllControls", false))
	{
		keymaps.push_back(strpair_t("K00:k",            "Joypad1 Right"));
		keymaps.push_back(strpair_t("K00:Right",        "Joypad1 Right"));
		keymaps.push_back(strpair_t("K00:h",            "Joypad1 Left"));
		keymaps.push_back(strpair_t("K00:Left",         "Joypad1 Left"));
		keymaps.push_back(strpair_t("K00:j",            "Joypad1 Down"));
		keymaps.push_back(strpair_t("K00:n",            "Joypad1 Down"));
		keymaps.push_back(strpair_t("K00:Down",         "Joypad1 Down"));
		keymaps.push_back(strpair_t("K00:u",            "Joypad1 Up"));
		keymaps.push_back(strpair_t("K00:Up",           "Joypad1 Up"));
		keymaps.push_back(strpair_t("K00:Return",       "Joypad1 Start"));
		keymaps.push_back(strpair_t("K00:space",        "Joypad1 Select"));
		keymaps.push_back(strpair_t("K00:S+d",          "Joypad1 ToggleTurbo A"));
		keymaps.push_back(strpair_t("K00:C+d",          "Joypad1 ToggleSticky A"));
		keymaps.push_back(strpair_t("K00:d",            "Joypad1 A"));
		keymaps.push_back(strpair_t("K00:S+c",          "Joypad1 ToggleTurbo B"));
		keymaps.push_back(strpair_t("K00:C+c",          "Joypad1 ToggleSticky B"));
		keymaps.push_back(strpair_t("K00:c",            "Joypad1 B"));
		keymaps.push_back(strpair_t("K00:S+s",          "Joypad1 ToggleTurbo X"));
		keymaps.push_back(strpair_t("K00:C+s",          "Joypad1 ToggleSticky X"));
		keymaps.push_back(strpair_t("K00:s",            "Joypad1 X"));
		keymaps.push_back(strpair_t("K00:S+x",          "Joypad1 ToggleTurbo Y"));
		keymaps.push_back(strpair_t("K00:C+x",          "Joypad1 ToggleSticky Y"));
		keymaps.push_back(strpair_t("K00:x",            "Joypad1 Y"));
		keymaps.push_back(strpair_t("K00:S+a",          "Joypad1 ToggleTurbo L"));
		keymaps.push_back(strpair_t("K00:S+v",          "Joypad1 ToggleTurbo L"));
		keymaps.push_back(strpair_t("K00:C+a",          "Joypad1 ToggleSticky L"));
		keymaps.push_back(strpair_t("K00:C+v",          "Joypad1 ToggleSticky L"));
		keymaps.push_back(strpair_t("K00:a",            "Joypad1 L"));
		keymaps.push_back(strpair_t("K00:v",            "Joypad1 L"));
		keymaps.push_back(strpair_t("K00:S+z",          "Joypad1 ToggleTurbo R"));
		keymaps.push_back(strpair_t("K00:C+z",          "Joypad1 ToggleSticky R"));
		keymaps.push_back(strpair_t("K00:z",            "Joypad1 R"));

		keymaps.push_back(strpair_t("K00:KP_Left",      "Joypad2 Left"));
		keymaps.push_back(strpair_t("K00:KP_Right",     "Joypad2 Right"));
		keymaps.push_back(strpair_t("K00:KP_Up",        "Joypad2 Up"));
		keymaps.push_back(strpair_t("K00:KP_Down",      "Joypad2 Down"));
		keymaps.push_back(strpair_t("K00:KP_Enter",     "Joypad2 Start"));
		keymaps.push_back(strpair_t("K00:KP_Add",       "Joypad2 Select"));
		keymaps.push_back(strpair_t("K00:Prior",        "Joypad2 A"));
		keymaps.push_back(strpair_t("K00:Next",         "Joypad2 B"));
		keymaps.push_back(strpair_t("K00:Home",         "Joypad2 X"));
		keymaps.push_back(strpair_t("K00:End",          "Joypad2 Y"));
		keymaps.push_back(strpair_t("K00:Insert",       "Joypad2 L"));
		keymaps.push_back(strpair_t("K00:Delete",       "Joypad2 R"));

		keymaps.push_back(strpair_t("K00:A+F4",         "SoundChannel0"));
		keymaps.push_back(strpair_t("K00:C+F4",         "SoundChannel0"));
		keymaps.push_back(strpair_t("K00:A+F5",         "SoundChannel1"));
		keymaps.push_back(strpair_t("K00:C+F5",         "SoundChannel1"));
		keymaps.push_back(strpair_t("K00:A+F6",         "SoundChannel2"));
		keymaps.push_back(strpair_t("K00:C+F6",         "SoundChannel2"));
		keymaps.push_back(strpair_t("K00:A+F7",         "SoundChannel3"));
		keymaps.push_back(strpair_t("K00:C+F7",         "SoundChannel3"));
		keymaps.push_back(strpair_t("K00:A+F8",         "SoundChannel4"));
		keymaps.push_back(strpair_t("K00:C+F8",         "SoundChannel4"));
		keymaps.push_back(strpair_t("K00:A+F9",         "SoundChannel5"));
		keymaps.push_back(strpair_t("K00:C+F9",         "SoundChannel5"));
		keymaps.push_back(strpair_t("K00:A+F10",        "SoundChannel6"));
		keymaps.push_back(strpair_t("K00:C+F10",        "SoundChannel6"));
		keymaps.push_back(strpair_t("K00:A+F11",        "SoundChannel7"));
		keymaps.push_back(strpair_t("K00:C+F11",        "SoundChannel7"));
		keymaps.push_back(strpair_t("K00:A+F12",        "SoundChannelsOn"));
		keymaps.push_back(strpair_t("K00:C+F12",        "SoundChannelsOn"));

		keymaps.push_back(strpair_t("K00:S+1",          "BeginRecordingMovie"));
		keymaps.push_back(strpair_t("K00:S+2",          "EndRecordingMovie"));
		keymaps.push_back(strpair_t("K00:S+3",          "LoadMovie"));
		keymaps.push_back(strpair_t("K00:A+F1",         "SaveSPC"));
		keymaps.push_back(strpair_t("K00:C+F1",         "SaveSPC"));
		keymaps.push_back(strpair_t("K00:F10",          "LoadOopsFile"));
		keymaps.push_back(strpair_t("K00:A+F2",         "LoadFreezeFile"));
		keymaps.push_back(strpair_t("K00:C+F2",         "LoadFreezeFile"));
		keymaps.push_back(strpair_t("K00:F11",          "LoadFreezeFile"));
		keymaps.push_back(strpair_t("K00:A+F3",         "SaveFreezeFile"));
		keymaps.push_back(strpair_t("K00:C+F3",         "SaveFreezeFile"));
		keymaps.push_back(strpair_t("K00:F12",          "SaveFreezeFile"));
		keymaps.push_back(strpair_t("K00:F1",           "QuickLoad000"));
		keymaps.push_back(strpair_t("K00:F2",           "QuickLoad001"));
		keymaps.push_back(strpair_t("K00:F3",           "QuickLoad002"));
		keymaps.push_back(strpair_t("K00:F4",           "QuickLoad003"));
		keymaps.push_back(strpair_t("K00:F5",           "QuickLoad004"));
		keymaps.push_back(strpair_t("K00:F6",           "QuickLoad005"));
		keymaps.push_back(strpair_t("K00:F7",           "QuickLoad006"));
		keymaps.push_back(strpair_t("K00:F8",           "QuickLoad007"));
		keymaps.push_back(strpair_t("K00:F9",           "QuickLoad008"));
		keymaps.push_back(strpair_t("K00:S+F1",         "QuickSave000"));
		keymaps.push_back(strpair_t("K00:S+F2",         "QuickSave001"));
		keymaps.push_back(strpair_t("K00:S+F3",         "QuickSave002"));
		keymaps.push_back(strpair_t("K00:S+F4",         "QuickSave003"));
		keymaps.push_back(strpair_t("K00:S+F5",         "QuickSave004"));
		keymaps.push_back(strpair_t("K00:S+F6",         "QuickSave005"));
		keymaps.push_back(strpair_t("K00:S+F7",         "QuickSave006"));
		keymaps.push_back(strpair_t("K00:S+F8",         "QuickSave007"));
		keymaps.push_back(strpair_t("K00:S+F9",         "QuickSave008"));

		keymaps.push_back(strpair_t("K00:Scroll_Lock",  "Pause"));
		keymaps.push_back(strpair_t("K00:CS+Escape",    "Reset"));
		keymaps.push_back(strpair_t("K00:S+Escape",     "SoftReset"));
		keymaps.push_back(strpair_t("K00:Escape",       "ExitEmu"));
		keymaps.push_back(strpair_t("K00:Tab",          "EmuTurbo"));
		keymaps.push_back(strpair_t("K00:S+Tab",        "ToggleEmuTurbo"));
		keymaps.push_back(strpair_t("K00:A+equal",      "IncEmuTurbo"));
		keymaps.push_back(strpair_t("K00:A+minus",      "DecEmuTurbo"));
		keymaps.push_back(strpair_t("K00:C+equal",      "IncTurboSpeed"));
		keymaps.push_back(strpair_t("K00:C+minus",      "DecTurboSpeed"));
		keymaps.push_back(strpair_t("K00:equal",        "IncFrameRate"));
		keymaps.push_back(strpair_t("K00:minus",        "DecFrameRate"));
		keymaps.push_back(strpair_t("K00:S+equal",      "IncFrameTime"));
		keymaps.push_back(strpair_t("K00:S+minus",      "DecFrameTime"));
		keymaps.push_back(strpair_t("K00:6",            "SwapJoypads"));
		keymaps.push_back(strpair_t("K00:Print",        "Screenshot"));

		keymaps.push_back(strpair_t("K00:1",            "ToggleBG0"));
		keymaps.push_back(strpair_t("K00:2",            "ToggleBG1"));
		keymaps.push_back(strpair_t("K00:3",            "ToggleBG2"));
		keymaps.push_back(strpair_t("K00:4",            "ToggleBG3"));
		keymaps.push_back(strpair_t("K00:5",            "ToggleSprites"));
		keymaps.push_back(strpair_t("K00:9",            "ToggleTransparency"));
		keymaps.push_back(strpair_t("K00:BackSpace",    "ClipWindows"));
		keymaps.push_back(strpair_t("K00:A+Escape",     "Debugger"));

		keymaps.push_back(strpair_t("M00:B0",           "{Mouse1 L,Superscope Fire,Justifier1 Trigger}"));
		keymaps.push_back(strpair_t("M00:B1",           "{Justifier1 AimOffscreen Trigger,Superscope AimOffscreen}"));
		keymaps.push_back(strpair_t("M00:B2",           "{Mouse1 R,Superscope Cursor,Justifier1 Start}"));
		keymaps.push_back(strpair_t("M00:Pointer",      "Pointer Mouse1+Superscope+Justifier1"));
		keymaps.push_back(strpair_t("K00:grave",        "Superscope ToggleTurbo"));
		keymaps.push_back(strpair_t("K00:slash",        "Superscope Pause"));
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

static int ErrorHandler (Display *display, XErrorEvent *event)
{
	return (0);
}


#include <SDL/SDL.h>
SDL_Surface *screen;

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
        screen = SDL_SetVideoMode(SNES_WIDTH * 2, SNES_HEIGHT_EXTENDED * 2, 16, 0);

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

	GFX.Screen = (uint16 *) (GUI.snes_buffer + (GFX.Pitch * 2 * 2)); // domaemon: Add 2 lines before drawing.

#ifndef HAVE_SDL // domaemon: this is used as the destination buffer in the original unix code.
	GUI.filter_buffer = (uint8 *) calloc((SNES_WIDTH * 2) * 2 * (SNES_HEIGHT_EXTENDED * 2), 1);
	if (!GUI.filter_buffer)
		FatalError("Failed to allocate GUI.filter_buffer.");
#endif

        GUI.blit_screen_pitch = SNES_WIDTH * 2 * 2; // window size =(*2); 2 byte pir pixel =(*2)
        GUI.blit_screen       = (uint8 *) screen->pixels;

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
#ifndef HAVE_SDL // FIXME: VideoLogger totally ignored.
	if (Settings.DumpStreams && isFrameBoundry)
		S9xVideoLogger(GUI.image->data, SNES_WIDTH * 2, SNES_HEIGHT_EXTENDED * 2, GUI.bytes_per_pixel, GUI.image->bytes_per_line);
#else
        SDL_Flip(screen);
#endif
}

#ifndef HAVE_SDL
static bool8 CheckForPendingXEvents (Display *display)
{
#ifdef SELECT_BROKEN_FOR_SIGNALS
	int	arg = 0;

	return (XEventsQueued(display, QueuedAlready) || (ioctl(ConnectionNumber(display), FIONREAD, &arg) == 0 && arg));
#else
	return (XPending(display));
#endif
}
#endif

void S9xProcessEvents (bool8 block)
{
#ifndef HAVE_SDL
	while (block || CheckForPendingXEvents(GUI.display))
	{
		XEvent	event;

		XNextEvent(GUI.display, &event);
		block = FALSE;

		switch (event.type)
		{
			case KeyPress:
			case KeyRelease:
				S9xReportButton(((event.xkey.state & (ShiftMask | Mod1Mask | ControlMask | Mod4Mask)) << 8) | event.xkey.keycode, event.type == KeyPress);
			#if 1
				{
					KeyCode	kc = XKeysymToKeycode(GUI.display, XKeycodeToKeysym(GUI.display, event.xkey.keycode, 0));
					if (event.xkey.keycode != kc)
						S9xReportButton(((event.xkey.state & (ShiftMask | Mod1Mask | ControlMask | Mod4Mask)) << 8) | kc, event.type == KeyPress);
				}
			#endif
				break;

			case FocusIn:
				SetXRepeat(FALSE);
				XFlush(GUI.display);
				break;

			case FocusOut:
				SetXRepeat(TRUE);
				XFlush(GUI.display);
				break;

			case ConfigureNotify:
				break;

			case ButtonPress:
			case ButtonRelease:
				S9xReportButton(0x40000000 | (event.xbutton.button - 1), event.type == ButtonPress);
				break;

			case Expose:
				Repaint(FALSE);
				break;
		}
	}
#else
	SDL_Event event;

	while ((block) || (SDL_PollEvent (&event) != 0)) {
	  switch (event.type) {
	  case SDL_KEYDOWN:
	  case SDL_KEYUP:
	    printf ("%d \n", event.key.keysym.scancode);
	    S9xReportButton(39, 1);
	    break;
	  case SDL_QUIT:
	    printf ("Quit Event. Bye.\n");

	    S9xExit();
	  }
	}

#endif
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
			KeyCode	kc;
			KeySym	ks;

			d |= 0x00000000;

			for (i = 4; n[i] != '\0' && n[i] != '+'; i++) ;

			if (n[i] == '\0' || i == 4)
				i = 4;
			else
			{
				for (i = 4; n[i] != '+'; i++)
				{
					switch (n[i])
					{
						case 'S': d |= ShiftMask   << 8; break;
						case 'C': d |= ControlMask << 8; break;
						case 'A': d |= Mod1Mask    << 8; break;
						case 'M': d |= Mod4Mask    << 8; break;
						default:  goto unrecog;
					}
				}

				i++;
			}

#ifndef HAVE_SDL
			if ((ks = XStringToKeysym(n + i)) == NoSymbol)
				goto unrecog;
			if ((kc = XKeysymToKeycode(GUI.display, ks)) == 0)
				goto unrecog;
#endif

			d |= kc & 0xff;

			return (S9xMapButton(d, *cmd, false));
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
