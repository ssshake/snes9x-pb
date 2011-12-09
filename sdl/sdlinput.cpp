#include <SDL/SDL.h>

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
			printf ("JOYHATMOTION\n");
			break;
		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			SDL_Quit();
		}
	}
}
	


			
	
