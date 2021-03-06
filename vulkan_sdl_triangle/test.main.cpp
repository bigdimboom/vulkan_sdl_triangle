#define SDL_MAIN_HANDLED
#include <iostream>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include <set>
#include <vector>
#include <limits>
#include <string>
#include <fstream>
#include <sstream>

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
vk::Format gSwapChainImageFormat;
vk::Extent2D gSwapChainExtent;
std::vector<vk::Image> gSwapChainImages;

std::vector<vk::UniqueImageView> gSwapChainImageViews;

vk::UniqueRenderPass gRenderPass;

vk::UniquePipelineLayout gPipelineLayout;
vk::UniquePipeline gGraphicsPipeline;

std::vector<vk::UniqueFramebuffer> gSwapChainFramebuffers;
vk::UniqueCommandPool gCommandPool;
std::vector<vk::UniqueCommandBuffer> gCommandBuffers;

std::vector<vk::UniqueSemaphore> gImageAvailableSemaphores;
std::vector<vk::UniqueSemaphore> gRenderFinishedSemaphores;
std::vector<vk::UniqueFence> gInFlightFences;


bool init();
void update();
void render();
void cleanup();

std::vector<char> readFile(const std::string& filename);


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
	catch (std::runtime_error err)
	{
		std::cout << "std::runtime_error: " << err.what() << std::endl;
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
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return false;
	}

	if (SDL_Vulkan_LoadLibrary(NULL))
	{
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

	////////////////////////////////////////////////////////////////////////////////////////////////////
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

	///////////////////////////////////////////////////////////////////////////////////////////////////////
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

	if (!gSelectedPhysicalDevice)
	{
		SDL_Log("failed to find a suitable GPU!");
		return false;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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


	///////////////////////////////////////////////////////////////////////////////////////////////////////
	// createImageViews
	gSwapChainImageViews.resize(gSwapChainImages.size());
	for (int i = 0; i < gSwapChainImageViews.size(); ++i)
	{
		vk::ImageSubresourceRange subresourceRange(
			vk::ImageAspectFlagBits::eColor,
			0,
			1,
			0,
			1
		);

		vk::ImageViewCreateInfo createInfo(vk::ImageViewCreateFlags(),
										   gSwapChainImages[i],
										   vk::ImageViewType::e2D,
										   gSwapChainImageFormat,
										   vk::ComponentMapping(),
										   subresourceRange);

		gSwapChainImageViews[i] = gDevice->createImageViewUnique(createInfo);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// createRenderPass
	// set framebuffer properties
	vk::AttachmentDescription colorAttachmentDesc;
	colorAttachmentDesc.setFormat(gSwapChainImageFormat);
	colorAttachmentDesc.setSamples(vk::SampleCountFlagBits::e1);
	colorAttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
	colorAttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eStore);
	colorAttachmentDesc.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
	colorAttachmentDesc.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
	colorAttachmentDesc.setInitialLayout(vk::ImageLayout::eUndefined);
	colorAttachmentDesc.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

	vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

	vk::SubpassDescription subpass(vk::SubpassDescriptionFlags(),
								   vk::PipelineBindPoint::eGraphics,
								   0, nullptr,
								   1, &colorAttachmentRef);

	vk::SubpassDependency dependency(
		0, 0,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::AccessFlags(),
		vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite
	);

	vk::RenderPassCreateInfo renderPassInfo(
		vk::RenderPassCreateFlags(),
		1, &colorAttachmentDesc,
		1, &subpass,
		1, &dependency
	);

	gRenderPass = gDevice->createRenderPassUnique(renderPassInfo);


	////////////////////////////////////////////////////////////////////////////////////////////
	// create graphics pipeline/shaders
	auto createShaderModule = [](const std::vector<char>& code) -> vk::UniqueShaderModule
	{
		vk::ShaderModuleCreateInfo shader_create_info;
		shader_create_info.setCodeSize(code.size());
		shader_create_info.setPCode(reinterpret_cast<const uint32_t*>(code.data()));
		return gDevice->createShaderModuleUnique(shader_create_info);
	};

	enum ShaderType
	{
		VERTEX_SHADER,
		FRAGMENT_SHADER
	};

	auto vertShaderCode = readFile("triangle.vert");
	auto fragShaderCode = readFile("triangle.frag");
	auto vertShaderModule = createShaderModule(vertShaderCode);
	auto fragShaderModule = createShaderModule(fragShaderCode);
	vk::PipelineShaderStageCreateInfo vertShaderStageInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eVertex, vertShaderModule.get(), "main");
	vk::PipelineShaderStageCreateInfo fragShaderStageInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eFragment, fragShaderModule.get(), "main");
	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	// the format of the vertex data that will be passed to the vertex shader
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vertexInputInfo.setVertexAttributeDescriptionCount(0);
	vertexInputInfo.setVertexBindingDescriptionCount(0);
	vertexInputInfo.setPVertexAttributeDescriptions(nullptr);
	vertexInputInfo.setPVertexBindingDescriptions(nullptr);

	// defines how geometry will be drawn, and if primitive restart should be enabled
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.setPrimitiveRestartEnable(VK_FALSE);
	inputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList);

	// viewport and scissor
	vk::Viewport viewport(0.0f, 0.0f, (float)gSwapChainExtent.width, (float)gSwapChainExtent.height, 0.0f, 1.0f);
	vk::Rect2D scissor(vk::Offset2D(0, 0), gSwapChainExtent);

	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.setViewportCount(1);
	viewportState.setScissorCount(1);
	viewportState.setPViewports(&viewport);
	viewportState.setPScissors(&scissor);

	// Rasterization
	vk::PipelineRasterizationStateCreateInfo rasterizerState(vk::PipelineRasterizationStateCreateFlags(),
															 VK_FALSE, VK_FALSE,
															 vk::PolygonMode::eFill,
															 vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise,
															 VK_FALSE, 0.0f,
															 VK_FALSE, 0.0f,
															 1.0f);

	// multiple sample anti-aliasing
	vk::PipelineMultisampleStateCreateInfo multisampleState(vk::PipelineMultisampleStateCreateFlags(),
															vk::SampleCountFlagBits::e1,
															VK_FALSE,
															1.0f,
															nullptr,
															VK_FALSE,
															VK_FALSE);

	// depth and stencil
	//vk::PipelineDepthStencilStateCreateInfo depth_n_stencil_state;


	// color blending
	vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
	colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	colorBlendAttachment.setSrcColorBlendFactor(vk::BlendFactor::eOne);
	colorBlendAttachment.setDstColorBlendFactor(vk::BlendFactor::eZero);
	colorBlendAttachment.setColorBlendOp(vk::BlendOp::eAdd);
	colorBlendAttachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
	colorBlendAttachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
	colorBlendAttachment.setAlphaBlendOp(vk::BlendOp::eAdd);

	vk::PipelineColorBlendStateCreateInfo colorBlendingState(vk::PipelineColorBlendStateCreateFlags(),
															 VK_FALSE, vk::LogicOp::eCopy,
															 1, &colorBlendAttachment,
															 { 0.0f, 0.0f, 0.0f, 0.0f });

	//// dynamic states
	//vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	//vk::PipelineDynamicStateCreateInfo dynamicState(vk::PipelineDynamicStateCreateFlags(),
	//												2, dynamicStates);

	// put all pipeline conponents together : VkPipelineLayout 
	vk::PipelineLayoutCreateInfo pipelineLayoutInfo(vk::PipelineLayoutCreateFlags(), 0, nullptr, 0, nullptr);
	gPipelineLayout = gDevice->createPipelineLayoutUnique(pipelineLayoutInfo);

	// creating pipeline
	vk::GraphicsPipelineCreateInfo pipelineInfo(
		vk::PipelineCreateFlags(),
		2, shaderStages,
		&vertexInputInfo,
		&inputAssembly,
		nullptr,
		&viewportState,
		&rasterizerState,
		&multisampleState,
		nullptr,
		&colorBlendingState
	);

	//pipelineInfo.setPDynamicState(&dynamicState);
	pipelineInfo.setLayout(gPipelineLayout.get());
	pipelineInfo.setRenderPass(gRenderPass.get());
	pipelineInfo.setSubpass(0);

	// the following are only considered if the VK_PIPELINE_CREATE_DERIVATIVE_BIT flag
	//pipelineInfo.setFlags(vk::PipelineCreateFlagBits::eAllowDerivatives);
	//pipelineInfo.setBasePipelineHandle(nullptr);
	//pipelineInfo.setBasePipelineIndex(-1);

	gGraphicsPipeline = gDevice->createGraphicsPipelineUnique(nullptr, pipelineInfo);

	// TODO:
	// frame buffers
	for (size_t i = 0; i < gSwapChainImageViews.size(); ++i)
	{
		vk::FramebufferCreateInfo framebufferInfo(
			vk::FramebufferCreateFlags(),
			gRenderPass.get(),
			1, &gSwapChainImageViews[i].get(),
			gSwapChainExtent.width, gSwapChainExtent.height,
			1
		);
		gSwapChainFramebuffers.push_back(gDevice->createFramebufferUnique(framebufferInfo));
	}

	// create command pool
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlags(),
		(uint32_t)gGraphicsQueueFamilyIndex
	);
	gCommandPool = gDevice->createCommandPoolUnique(poolInfo);

	// command buffer allocations
	vk::CommandBufferAllocateInfo cmdBufferAllocInfo(
		gCommandPool.get(),
		vk::CommandBufferLevel::ePrimary,
		(uint32_t)gSwapChainFramebuffers.size()
	);
	gCommandBuffers = gDevice->allocateCommandBuffersUnique(cmdBufferAllocInfo);

	// record command
	for (size_t i = 0; i < gCommandBuffers.size(); i++)
	{
		gCommandBuffers[i]->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse));

		vk::ClearValue clearColor;
		clearColor.color.setFloat32({ 0.0f, 0.0f, 0.0f, 1.0f });

		vk::RenderPassBeginInfo renderPassBeginInfo(
			gRenderPass.get(),
			gSwapChainFramebuffers[i].get(),
			vk::Rect2D(vk::Offset2D(0, 0), gSwapChainExtent),
			1, &clearColor
		);

		gCommandBuffers[i]->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		gCommandBuffers[i]->bindPipeline(vk::PipelineBindPoint::eGraphics, gGraphicsPipeline.get());
		gCommandBuffers[i]->draw(3, 1, 0, 0);
		gCommandBuffers[i]->endRenderPass();

		gCommandBuffers[i]->end();
	}

	// create semaphores
	// one semaphore to signal that an image has been acquired and is ready for rendering, 
	// and another one to signal that rendering has finished and presentation can happen

	for (int i = 0; i < gCommandBuffers.size(); ++i)
	{
		gImageAvailableSemaphores.push_back(gDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
		gRenderFinishedSemaphores.push_back(gDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
		gInFlightFences.push_back(gDevice->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)));
	}

	return true;
}

void update()
{
}

void render()
{
	//index refers to the VkImage in our swapChainImages array. We're going to use that index to pick the right command buffer.
	static uint32_t currentFrame = 0;
	static uint32_t imageIndex = -1;

	gDevice->waitForFences(1, &gInFlightFences[currentFrame].get(), VK_TRUE, std::numeric_limits<uint64_t>::max());
	gDevice->resetFences(1, &gInFlightFences[currentFrame].get());

	imageIndex = gDevice->acquireNextImageKHR(gSwapChain, std::numeric_limits<uint64_t>::max(), gImageAvailableSemaphores[currentFrame].get(), nullptr).value;

	vk::PipelineStageFlags flags[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

	vk::SubmitInfo submitInfo(
		1, &gImageAvailableSemaphores[currentFrame].get(),
		flags,
		1, &gCommandBuffers[imageIndex].get(),
		1, &gRenderFinishedSemaphores[imageIndex].get()
	);

	gGraphicsQueue.submit(1, &submitInfo, gInFlightFences[currentFrame].get());

	vk::PresentInfoKHR presentInfo(
		1, &gRenderFinishedSemaphores[currentFrame].get(),
		1, &gSwapChain,
		&imageIndex
	);

	gGraphicsQueue.presentKHR(presentInfo);

	//std::cout << "image index " << imageIndex << "\n";
	//std::cout << "current frame " << currentFrame << "\n";

	currentFrame = (currentFrame + 1) % gCommandBuffers.size();

}

void cleanup()
{
	gDevice->waitIdle();

	gDevice->destroySwapchainKHR(gSwapChain);
	gVKInstance->destroySurfaceKHR(gSurface);
	SDL_DestroyWindow(gWindow);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
	gWindow = nullptr;
}

std::vector<char> readFile(const std::string & filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

