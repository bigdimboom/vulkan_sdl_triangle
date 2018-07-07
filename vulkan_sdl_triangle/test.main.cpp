#define SDL_MAIN_HANDLED
#include <iostream>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.hpp>

#include <vector>

SDL_Window* gWindow = nullptr;
const std::string gWindow_title = "SDL_VULKAN_TIANGLE";
const int gWindowWidth = 1280;
const int gWindowHeight = 800;

static char const* AppName = "sdl_vulkan_triangle";
static char const* EngineName = "vulkan.hpp";


int main(int argc, const char** argv)
{
	try
	{

		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
			SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
			return EXIT_FAILURE;
		}

		if (SDL_Vulkan_LoadLibrary(NULL)) {
			SDL_Log("Unable to initialize vulkan lib: %s", SDL_GetError());
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

		uint32_t nExt = 0;
		std::vector<const char*> vulkan_extensions;
		if (!SDL_Vulkan_GetInstanceExtensions(gWindow, &nExt, NULL))
		{
			SDL_Log("Unable to get vulkan extension names: %s", SDL_GetError());
			return EXIT_FAILURE;
		}
		vulkan_extensions.resize(nExt);
		if (!SDL_Vulkan_GetInstanceExtensions(gWindow, &nExt, &vulkan_extensions[0]))
		{
			SDL_Log("Unable to get vulkan extension names: %s", SDL_GetError());
			return EXIT_FAILURE;
		}

		// extension names
		static const char *const additionalExtensions[] =
		{
			VK_EXT_DEBUG_REPORT_EXTENSION_NAME // example additional extension
		};
		size_t additionalExtensionsCount = sizeof(additionalExtensions) / sizeof(additionalExtensions[0]);
		vulkan_extensions.resize(vulkan_extensions.size() + additionalExtensionsCount);
		for (size_t i = 0; i < additionalExtensionsCount; i++)
		{
			vulkan_extensions[nExt + i] = additionalExtensions[i];
		}

		// init vulkan instance
		vk::ApplicationInfo appInfo(AppName, 1, EngineName, 1, VK_API_VERSION_1_1);
		vk::InstanceCreateInfo instanceCreateInfo;
		instanceCreateInfo.setPApplicationInfo(&appInfo);
		instanceCreateInfo.setEnabledExtensionCount((uint32_t)vulkan_extensions.size());
		instanceCreateInfo.setPpEnabledExtensionNames(&vulkan_extensions[0]);
		vk::UniqueInstance vkInstance = vk::createInstanceUnique(instanceCreateInfo);

		// enumerate the physicalDevices and select one
		std::vector<vk::PhysicalDevice> physicalDevices = vkInstance->enumeratePhysicalDevices();
		assert(!physicalDevices.empty());
		vk::PhysicalDevice selected = nullptr;
		size_t graphicsQueueFamilyIndex = -1;
		for (const auto& dev : physicalDevices)
		{
			auto queueFamilies = dev.getQueueFamilyProperties();
			int count = 0;
			for (const auto& queueFamily : queueFamilies) 
			{
				if (queueFamily.queueCount > 0 && 
				    (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eCompute))
				{
					selected = dev;
					graphicsQueueFamilyIndex = count;
					break;
				}
				++count;
			}

			if (selected)
			{
				break;
			}
		}

		if (!selected) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}










		SDL_DestroyWindow(gWindow);
		SDL_Vulkan_UnloadLibrary();
		SDL_Quit();
		gWindow = nullptr;
	}
	catch (vk::SystemError err)
	{
		std::cout << "vk::SystemError: " << err.what() << std::endl;
		exit(-1);
	}
	catch (...)
	{
		std::cout << "unknown error\n";
		exit(-1);
	}

	return EXIT_SUCCESS;
}