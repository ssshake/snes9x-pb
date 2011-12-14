#include <SDL/SDL.h>
#include "sdl_snes9x.h"

#include "snes9x.h"
#include "port.h"
#include "controls.h"

SDL_Joystick * joystick[4] = {NULL, NULL, NULL, NULL};
int num_joysticks = 0;
//ConfigFile::secvec_t	keymaps;
using namespace std;
std::map <string, int> name_sdlkeysym;

// domaemon: FIXME, just collecting the essentials.
// domaemon: *) here we define the keymapping.
void S9xParseInputConfig (ConfigFile &conf, int pass)
{
	keymaps.clear();
	if (!conf.GetBool("Unix::ClearAllControls", false))
	{
		// Using 'Joypad# Axis'
		keymaps.push_back(strpair_t("J00:Axis0",      "Joypad1 Axis Left/Right T=50%"));
		keymaps.push_back(strpair_t("J00:Axis1",      "Joypad1 Axis Up/Down T=50%"));

		keymaps.push_back(strpair_t("J00:B0",         "Joypad1 X"));
		keymaps.push_back(strpair_t("J00:B1",         "Joypad1 A"));
		keymaps.push_back(strpair_t("J00:B2",         "Joypad1 B"));
		keymaps.push_back(strpair_t("J00:B3",         "Joypad1 Y"));

		keymaps.push_back(strpair_t("J00:B6",         "Joypad1 L"));
		keymaps.push_back(strpair_t("J00:B7",         "Joypad1 R"));
		keymaps.push_back(strpair_t("J00:B8",         "Joypad1 Select"));
		keymaps.push_back(strpair_t("J00:B11",        "Joypad1 Start"));
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

	return;
}

void S9xInitInputDevices (void)
{
	// domaemon: 1) initializing the joystic subsystem
	SDL_InitSubSystem (SDL_INIT_JOYSTICK);

	/*
	 * domaemon: 2) check how may joysticks are connected
	 * domaemon: 3) relate paddev1 to SDL_Joystick[0], paddev2 to SDL_Joystick[1]...
	 * domaemon: 4) print out the joystick name and capabilities
	 */

	num_joysticks = SDL_NumJoysticks();

	if (num_joysticks == 0)
	{
		fprintf(stderr, "joystick: No joystick found.\n");
	}
	else
	{
		SDL_JoystickEventState (SDL_ENABLE);

		for (int i = 0; i < num_joysticks; i++)
		{
			joystick[i] = SDL_JoystickOpen (i);
			printf ("  %s\n", SDL_JoystickName(i));
			printf ("  %d-axis %d-buttons %d-balls %d-hats \n",
				SDL_JoystickNumAxes(joystick[i]),
				SDL_JoystickNumButtons(joystick[i]),
				SDL_JoystickNumBalls(joystick[i]),
				SDL_JoystickNumHats(joystick[i]));
		}
	}
}

void S9xProcessEvents (bool8 block)
{
	SDL_Event event;
	bool8 quit_state = FALSE;

	while ((block) || (SDL_PollEvent (&event) != 0))
	{
		switch (event.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			// domaemon: not sure it's the best idea, but reserving the SDLK_q for quit.
			if (event.key.keysym.sym == SDLK_q)
			{
				quit_state = TRUE;
			} 
			else
			{
				S9xReportButton(event.key.keysym.mod << 16 | // keyboard mod
						event.key.keysym.sym, // keyboard ksym
						event.type == SDL_KEYDOWN); // press or release
			}
			break;

/***** Joystick starts *****/
#if 0
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			S9xReportButton(0x80000000 | // joystick button
					(event.jbutton.which << 24) | // joystick index
					event.jbutton.button, // joystick button code
					event.type == SDL_JOYBUTTONDOWN); // press or release
			break;

		case SDL_JOYAXISMOTION:
			S9xReportAxis(0x80008000 | // joystick axis
				      (event.jaxis.which << 24) | // joystick index
				      event.jaxis.axis, // joystick axis
				      event.jaxis.value); // axis value
			break;
#endif
/***** Joystick ends *****/

		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			quit_state = TRUE;
			break;
		}
	}
	
	if (quit_state == TRUE)
	{
		printf ("Quit Event. Bye.\n");
		S9xExit();
	}
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
	return (false);
}

bool S9xPollButton (uint32 id, bool *pressed)
{
	return (S9xDisplayPollButton(id, pressed));
}

bool S9xPollAxis (uint32 id, int16 *value)
{
	return (S9xDisplayPollAxis(id, value));
}

bool S9xPollPointer (uint32 id, int16 *x, int16 *y)
{
	return (S9xDisplayPollPointer(id, x, y));
}

// domaemon: needed by SNES9X
void S9xHandlePortCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{
	return;
}


#if 0

main()
{
	int i;
	SDL_Joystick *joystick;
	SDL_Event event;
	SDL_Surface *screen;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0)
	{
		printf("Unable to initialize SDL: %s\n", SDL_GetError());
	}
  
	atexit(SDL_Quit);

	screen = SDL_SetVideoMode(64, 64, 16, 0);

	for (i = 0; i < SDL_NumJoysticks(); i++)
	{
		printf ("  %s\n", SDL_JoystickName(i));
	}

	SDL_JoystickEventState (SDL_ENABLE);
	joystick = SDL_JoystickOpen (0);

	while (SDL_WaitEvent (&event) != 0) 
	{
		switch (event.type) {
		case SDL_KEYDOWN:
			printf ("KEYDOWN\n");
			break;
		case SDL_JOYAXISMOTION:
			printf ("JOYAXISMOTION\n");
			break;
		case SDL_JOYBUTTONDOWN:
			printf ("JOYBUTTONDOWN\n");
			printf ("%d \n", event.jbutton.button);
			break;
		case SDL_JOYHATMOTION:
			switch (event.jhat.value) {
			case SDL_HAT_UP:
				printf ("SDL_HAT_UP\n");
				break;
			case SDL_HAT_DOWN:
				printf ("SDL_HAT_DOWN\n");
				break;
			case SDL_HAT_LEFT:
				printf ("SDL_HAT_LEFT\n");
				break;
			case SDL_HAT_RIGHT:
				printf ("SDL_HAT_RIGHT\n");
				break;
			}

			printf ("JOYHATMOTION\n");
			break;
		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			SDL_Quit();
		}
	}
}
	
#endif
