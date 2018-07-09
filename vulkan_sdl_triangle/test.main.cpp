#define SDL_MAIN_HANDLED
#include <iostream>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include <set>
#include <vector>
#include <limits>

SDL_Window* gWindow = nullptr;
const std::string gWindow_title = "SDL_VULKAN_TIANGLE";
const int gWindowWidth = 1280;
const int gWindowHeight = 800;

static char const* AppName = "sdl_vulkan_triangle";
static char const* EngineName = "vulkan.hpp";

const std::vector<const char*> validationLayers = {
	"VK_LAYER_LUNARG_standard_validation"
};

const std::vector<const char*> deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

vk::UniqueInstance gVKInstance;
vk::SurfaceKHR gSurface;

vk::UniqueDevice gDevice;
vk::PhysicalDevice gSelectedPhysicalDevice = nullptr;
size_t gGraphicsQueueFamilyIndex = -1;
size_t gPresentQueueFamilyIndex = -1;
vk::Queue gGraphicsQueue;
vk::Queue gPresentQueue;

vk::SwapchainKHR gSwapChain;
std::vector<vk::Image> gSwapChainImages;
vk::Format gSwapChainImageFormat;
vk::Extent2D gSwapChainExtent;

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
	instanceCreateInfo.setEnabledLayerCount((uint32_t)validationLayers.size());
	instanceCreateInfo.setPpEnabledLayerNames(validationLayers.data());
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
		gGraphicsQueueFamilyIndex = -1;
		gPresentQueueFamilyIndex = -1;

		auto queueFamilies = dev.getQueueFamilyProperties();
		int count = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			// query if graphics queue 
			if (queueFamily.queueCount > 0 &&
				(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) /*&&
				(queueFamily.queueFlags & vk::QueueFlagBits::eCompute)*/)
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
				goto Next;
			}

			++count;
		}
	}

Next:

	if (!gSelectedPhysicalDevice) {
		SDL_Log("failed to find a suitable GPU!");
		return false;
	}


	// create logic device
	float queuePriority = 1.0f;
	std::set<size_t> uniqueQueueFamilies = { gGraphicsQueueFamilyIndex, gPresentQueueFamilyIndex };
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;

	for (const auto& family_queue_index : uniqueQueueFamilies)
	{
		queueCreateInfos.push_back(vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), static_cast<uint32_t>(gGraphicsQueueFamilyIndex), 1, &queuePriority));
	}

	vk::DeviceCreateInfo device_create_info(vk::DeviceCreateFlags(), (uint32_t)queueCreateInfos.size(),
											queueCreateInfos.data(), (uint32_t)validationLayers.size(), validationLayers.data(),
											(uint32_t)deviceExtensions.size(), deviceExtensions.data());

	gDevice = gSelectedPhysicalDevice.createDeviceUnique(device_create_info);

	gGraphicsQueue = gDevice->getQueue((uint32_t)gGraphicsQueueFamilyIndex, 0);
	gPresentQueue = gDevice->getQueue((uint32_t)gPresentQueueFamilyIndex, 0);


	// TODO:
	// crate swap chain
	struct SwapChainSupportDetails
	{
		vk::SurfaceCapabilitiesKHR capabilities;
		std::vector<vk::SurfaceFormatKHR> formats;
		std::vector<vk::PresentModeKHR> presentModes;
	};

	auto querySwapChainSupport = [](vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) -> SwapChainSupportDetails
	{
		SwapChainSupportDetails swapChainSupport;

		swapChainSupport.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
		swapChainSupport.formats = physicalDevice.getSurfaceFormatsKHR(surface);
		swapChainSupport.presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

		return swapChainSupport;
	};

	auto chooseSwapSurfaceFormat = [](const std::vector<vk::SurfaceFormatKHR>& availableFormats) -> vk::SurfaceFormatKHR
	{
		if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
		{
			vk::SurfaceFormatKHR surfaceformat;
			surfaceformat.format = vk::Format::eB8G8R8A8Unorm;
			surfaceformat.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
			return surfaceformat;
		}

		for (const auto& sFormat : availableFormats)
		{
			if (sFormat.format == vk::Format::eB8G8R8A8Unorm && sFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			{
				return sFormat;
			}
		}

		assert(availableFormats.size() > 0 && "should not reach here");
		return availableFormats[0];
	};

	auto chooseSwapPresentMode = [](const std::vector<vk::PresentModeKHR>& availablePresentModes) ->vk::PresentModeKHR
	{
		vk::PresentModeKHR bestMode = vk::PresentModeKHR::eFifo;

		for (const auto& mode : availablePresentModes)
		{
			switch (mode)
			{
			case vk::PresentModeKHR::eMailbox:
				return mode;
			case vk::PresentModeKHR::eImmediate:
				bestMode = mode;
				break;
			default:
				break;
			}
		}
		return bestMode;
	};

	auto chooseSwapExtent = [](const vk::SurfaceCapabilitiesKHR capabilities) ->vk::Extent2D
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}

		vk::Extent2D actualExtent;
		actualExtent.setWidth(gWindowWidth);
		actualExtent.setHeight(gWindowHeight);
		actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
		return actualExtent;
	};


	auto swapChainSupport = querySwapChainSupport(gSelectedPhysicalDevice, surface);

	auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	auto extent = chooseSwapExtent(swapChainSupport.capabilities);

	// we'll try to have one more than that to properly implement triple buffering.
	uint32_t imageCount = glm::clamp(swapChainSupport.capabilities.minImageCount + 1,
									 swapChainSupport.capabilities.minImageCount,
									 swapChainSupport.capabilities.maxImageCount);

	vk::SwapchainCreateInfoKHR swapChainCreateInfo(
		vk::SwapchainCreateFlagsKHR(),
		gSurface,
		imageCount,
		surfaceFormat.format,
		surfaceFormat.colorSpace,
		extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment,
		vk::SharingMode::eExclusive
	);

	uint32_t queueFamilyIndices[] = { (uint32_t)gGraphicsQueueFamilyIndex, (uint32_t)gPresentQueueFamilyIndex };

	if (gGraphicsQueueFamilyIndex != gPresentQueueFamilyIndex)
	{
		swapChainCreateInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
		swapChainCreateInfo.setQueueFamilyIndexCount(2);
		swapChainCreateInfo.setPQueueFamilyIndices(&queueFamilyIndices[0]);
	}
	else
	{
		swapChainCreateInfo.setImageSharingMode(vk::SharingMode::eExclusive);
		swapChainCreateInfo.setQueueFamilyIndexCount(1);
		swapChainCreateInfo.setPQueueFamilyIndices(&queueFamilyIndices[0]);
	}

	swapChainCreateInfo.setPreTransform(swapChainSupport.capabilities.currentTransform);
	swapChainCreateInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
	swapChainCreateInfo.setPresentMode(presentMode);
	swapChainCreateInfo.setClipped(VK_TRUE);
	swapChainCreateInfo.setOldSwapchain(nullptr);

	gSwapChain = gDevice->createSwapchainKHR(swapChainCreateInfo);

	// get images in swap chain
	gSwapChainImages = gDevice->getSwapchainImagesKHR(gSwapChain);
	gSwapChainImageFormat = surfaceFormat.format;
	gSwapChainExtent = extent;

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
	gDevice->destroySwapchainKHR(gSwapChain);
	gVKInstance->destroySurfaceKHR(gSurface);
	SDL_DestroyWindow(gWindow);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
	gWindow = nullptr;
}
