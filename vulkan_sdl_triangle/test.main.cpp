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

vk::UniqueInstance gVKInstance;
vk::SurfaceKHR gSurface;
vk::PhysicalDevice gSelectedPhysicalDevice = nullptr;
size_t gGraphicsQueueFamilyIndex = -1;
size_t gPresentQueueFamilyIndex = -1;
vk::UniqueDevice gDevice;

bool init();
void update();
void render();
void cleanup();

int main(int argc, const char** argv)
{
	try
	{
		if (!init())
		{
			return EXIT_FAILURE;
		}

		// main loop
		bool running = true;
		SDL_Event ev;
		while (running)
		{
			while (SDL_PollEvent(&ev))
			{
				if (ev.type == SDL_QUIT)
				{
					running = false;
				}
			}

			update();
			render();
		}


		cleanup();
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

bool init()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return false;
	}

	if (SDL_Vulkan_LoadLibrary(NULL)) {
		SDL_Log("Unable to initialize vulkan lib: %s", SDL_GetError());
		return false;
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
		return false;
	}

	uint32_t nExt = 0;
	std::vector<const char*> vulkan_extensions;
	if (!SDL_Vulkan_GetInstanceExtensions(gWindow, &nExt, NULL))
	{
		SDL_Log("Unable to get vulkan extension names: %s", SDL_GetError());
		return false;
	}
	vulkan_extensions.resize(nExt);
	if (!SDL_Vulkan_GetInstanceExtensions(gWindow, &nExt, &vulkan_extensions[0]))
	{
		SDL_Log("Unable to get vulkan extension names: %s", SDL_GetError());
		return false;
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
	gVKInstance = vk::createInstanceUnique(instanceCreateInfo);

	// TODO:
	//// create debug callback
	//vk::DebugReportCallbackCreateInfoEXT debug_report_create_info;
	//debug_report_create_info.flags =
	//	vk::DebugReportFlagBitsEXT::eDebug |
	//	vk::DebugReportFlagBitsEXT::eError |
	//	vk::DebugReportFlagBitsEXT::eInformation |
	//	vk::DebugReportFlagBitsEXT::ePerformanceWarning |
	//	vk::DebugReportFlagBitsEXT::eWarning;
	//debug_report_create_info.pfnCallback = nullptr;
	//auto debugCBHandle = vkInstance->createDebugReportCallbackEXTUnique(debug_report_create_info);

	// Create Surface
	SDL_vulkanSurface surface = nullptr;
	if (!SDL_Vulkan_CreateSurface(gWindow, gVKInstance.get(), &surface))
	{
		throw std::runtime_error("failed to create window surface!");
	}
	gSurface = surface;

	// enumerate the physicalDevices and select one and its queue family index
	std::vector<vk::PhysicalDevice> physicalDevices = gVKInstance->enumeratePhysicalDevices();
	assert(!physicalDevices.empty());
	for (const auto& dev : physicalDevices)
	{
		if (gSelectedPhysicalDevice)
		{
			break;
		}

		gGraphicsQueueFamilyIndex = -1;
		gPresentQueueFamilyIndex = -1;

		auto queueFamilies = dev.getQueueFamilyProperties();
		int count = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			// query if graphics queue 
			if (queueFamily.queueCount > 0 &&
				(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
				/*&&(queueFamily.queueFlags & vk::QueueFlagBits::eCompute)*/)
			{
				gGraphicsQueueFamilyIndex = count;
			}

			// query if support present queue
			if (queueFamily.queueCount > 0 && dev.getSurfaceSupportKHR(count, gSurface))
			{
				gPresentQueueFamilyIndex = count;
			}

			if (gGraphicsQueueFamilyIndex != -1 &&
				gPresentQueueFamilyIndex != -1)
			{
				gSelectedPhysicalDevice = dev;
				break;
			}

			++count;
		}
	}

	if (!gSelectedPhysicalDevice) {
		SDL_Log("failed to find a suitable GPU!");
		return false;
	}

	// TODO:
	// create logic device
	float queuePriority = 0.0f;
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos =
	{
		vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), static_cast<uint32_t>(gGraphicsQueueFamilyIndex), 1, &queuePriority),
		vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), static_cast<uint32_t>(gPresentQueueFamilyIndex), 1, &queuePriority)
	};
	gDevice = gSelectedPhysicalDevice.createDeviceUnique(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos.size(), queueCreateInfos.data()));

	//auto graphicsQueue = gDevice->getQueue(gGraphicsQueueFamilyIndex, 0);
	//auto presentQueue = gDevice->getQueue(gPresentQueueFamilyIndex, 0);



	return true;
}

void update()
{
}

void render()
{
}

void cleanup()
{
	gVKInstance->destroySurfaceKHR(gSurface);
	SDL_DestroyWindow(gWindow);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
	gWindow = nullptr;
}
