#define SDL_MAIN_HANDLED
#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

SDL_Window* gWindow = nullptr;
const std::string gWindow_title = "SDL_VULKAN_TIANGLE";
const int gWindowWidth = 1280;
const int gWindowHeight = 800;



int main(int argc, const char** argv)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	gWindow = SDL_CreateWindow(gWindow_title.c_str(), 
							   SDL_WINDOWPOS_CENTERED, 
							   SDL_WINDOWPOS_CENTERED, 
							   gWindowWidth, gWindowHeight, 
							   SDL_WINDOW_VULKAN | 
							   SDL_WINDOW_SHOWN);

	if (!gWindow)
	{
		SDL_Log("Unable to initialize vulkan window: %s", SDL_GetError());
		return EXIT_FAILURE;
	}










	SDL_DestroyWindow(gWindow);
	SDL_Quit();
	gWindow = nullptr;
	return EXIT_SUCCESS;
}