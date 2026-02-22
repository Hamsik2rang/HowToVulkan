/* Copyright (c) 2025-2026, Sascha Willems
 * SPDX-License-Identifier: MIT
 */

#define VOLK_IMPLEMENTATION
#include <vulkan/vulkan.h>
#include <volk/volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <fstream>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "slang/slang.h"
#include "slang/slang-com-ptr.h"
#include <ktx.h>
#include <ktxvulkan.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

constexpr uint32_t g_MaxFramesInFlight{ 2 };
uint32_t g_ImageIndex{ 0 };
uint32_t g_FrameIndex{ 0 };
VkInstance g_Instance{ VK_NULL_HANDLE };

VkPhysicalDevice g_PhysicalDevice{ VK_NULL_HANDLE };
VkDevice g_Device{ VK_NULL_HANDLE };
VkQueue g_Queue{ VK_NULL_HANDLE };
uint32_t g_QueueFamilyIndex = 0;

VmaAllocator g_Allocator{ VK_NULL_HANDLE };

SDL_Window* g_SDLWindow{ nullptr };
VkSurfaceKHR g_Surface{ VK_NULL_HANDLE };
VkSurfaceCapabilitiesKHR g_SurfaceCaps{};
bool g_bUpdateSwapchain{ false };
VkSwapchainKHR g_Swapchain{ VK_NULL_HANDLE };
uint32_t g_SwapchainImageCount{ 0 };

VkShaderModule g_ShaderModule{ VK_NULL_HANDLE };

VkCommandPool g_CommandPool{ VK_NULL_HANDLE };
VkPipeline g_Pipeline{ VK_NULL_HANDLE };
VkPipelineLayout g_PipelineLayout{ VK_NULL_HANDLE };

VkImage g_DepthImage{ VK_NULL_HANDLE };
VkFormat g_DepthImageFormat{ VK_FORMAT_UNDEFINED };
VmaAllocation g_DepthImageAllocation{ nullptr };
VkImageView g_DepthImageView{ VK_NULL_HANDLE };

const VkFormat g_kSwapchainImageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
std::vector<VkImage> g_SwapchainImages;
std::vector<VkImageView> g_SwapchainImageViews;

std::array<VkCommandBuffer, g_MaxFramesInFlight> g_CommandBuffers;
std::array<VkFence, g_MaxFramesInFlight> g_Fences;
std::array<VkSemaphore, g_MaxFramesInFlight> g_PresentSemaphores;
std::vector<VkSemaphore> g_RenderSemaphores;

VmaAllocation g_VertexBuferAllocation{ VK_NULL_HANDLE };

struct VertexBuffer
{
	VkBuffer handle;
	VkDeviceSize size;
	VkDeviceSize vertexOffset;
	VkDeviceSize vertexSize;
	// IndexOffset과 IndexSize는 전체 크기와 VertexOffset, Size로 알 수 있지만 일반화를 위해 사용합니다.
	VkDeviceSize indexOffset;
	VkDeviceSize indexSize;
	VkIndexType indexType;

} g_VertexBuffer{};

struct ShaderData
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model[3];
	glm::vec4 lightPos{ 0.0f, -10.0f, 10.0f, 0.0f };
	uint32_t selected{ 1 };
} g_ShaderData{};

struct ShaderDataBuffer
{
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VkBuffer buffer{ VK_NULL_HANDLE };
	VkDeviceAddress deviceAddress{};
	void* mapped{ nullptr };
};
std::array<ShaderDataBuffer, g_MaxFramesInFlight> g_ShaderDataBuffers;

struct Texture
{
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VkImage image{ VK_NULL_HANDLE };
	VkImageView view{ VK_NULL_HANDLE };
	VkSampler sampler{ VK_NULL_HANDLE };
};
std::array<Texture, 3> g_Textures{};

VkDescriptorPool g_DescriptorPool{ VK_NULL_HANDLE };
VkDescriptorSetLayout g_DescriptorSetLayoutTex{ VK_NULL_HANDLE };
VkDescriptorSet g_DescriptorSetTex{ VK_NULL_HANDLE };

Slang::ComPtr<slang::IGlobalSession> g_SlangGlobalSession;

glm::vec3 g_CamPos{ 0.0f, 0.0f, -6.0f };
glm::vec3 g_ObjectRotations[3]{};
glm::ivec2 g_WindowSize{};

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
};

static inline void chk(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		std::cerr << "Vulkan call returned an error (" << result << ")\n";
		exit(result);
	}
}
static inline void chkSwapchain(VkResult result)
{
	if (result < VK_SUCCESS)
	{
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_bUpdateSwapchain = true;
			return;
		}
		std::cerr << "Vulkan call returned an error (" << result << ")\n";
		exit(result);
	}
}
static inline void chk(bool result)
{
	if (!result)
	{
		std::cerr << "Call returned an error\n";
		exit(result);
	}
}

void CreateInstance();
void CreateDevice(int deviceIndex);
void CreateAllocator();
void CreateSurface();
void CreateSwapchain(VkSwapchainKHR oldSwapchain);
void CreateDepthImage(uint32_t width, uint32_t height);
void LoadMeshAndCreateVertexDatas();
void CreateShaderDataBuffers();
void InitializeTextures(std::vector<VkDescriptorImageInfo>& textureDesc);
void CreateSyncObjects();

void CreateDescriptorSetLayout();
void CreateDescriptorSetPool();
void AllocateDescriptorSet();

void CreateGraphicsPipeline();

void TearDown();

int main(int argc, char* argv[])
{
	chk(SDL_Init(SDL_INIT_VIDEO));
	chk(SDL_Vulkan_LoadLibrary(NULL));
	volkInitialize();

	CreateInstance();
	int deviceIndex = 0;
	if (argc > 1)
	{
		deviceIndex = std::stoi(argv[1]);
	}
	CreateDevice(deviceIndex);
	CreateAllocator();
	
	CreateSurface();
	CreateSwapchain(nullptr);
	CreateDepthImage(static_cast<uint32_t>(g_WindowSize.x), static_cast<uint32_t>(g_WindowSize.y));

	LoadMeshAndCreateVertexDatas();
	CreateShaderDataBuffers();
	CreateSyncObjects();

	// Command pool
	VkCommandPoolCreateInfo commandPoolCI
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(g_QueueFamilyIndex)
	};
	chk(vkCreateCommandPool(g_Device, &commandPoolCI, nullptr, &g_CommandPool));

	VkCommandBufferAllocateInfo cbAllocCI
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = g_CommandPool,
		.commandBufferCount = g_MaxFramesInFlight
	};
	chk(vkAllocateCommandBuffers(g_Device, &cbAllocCI, g_CommandBuffers.data()));


	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	InitializeTextures(textureDescriptors);
	
	// Initialize Slang shader compiler
	slang::createGlobalSession(g_SlangGlobalSession.writeRef());
	auto slangTargets{ std::to_array<slang::TargetDesc>({ {.format{SLANG_SPIRV}, .profile{g_SlangGlobalSession->findProfile("spirv_1_4")} } }) };
	auto slangOptions{ std::to_array<slang::CompilerOptionEntry>({ { slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1} } }) };
	slang::SessionDesc slangSessionDesc{ .targets{slangTargets.data()}, .targetCount{SlangInt(slangTargets.size())}, .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR, .compilerOptionEntries{slangOptions.data()}, .compilerOptionEntryCount{uint32_t(slangOptions.size())} };

	// Load shader
	Slang::ComPtr<slang::ISession> slangSession;
	g_SlangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());
	Slang::ComPtr<slang::IModule> slangModule
	{ 
		slangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr) 
	};
	Slang::ComPtr<ISlangBlob> spirv;
	slangModule->getTargetCode(0, spirv.writeRef());
	VkShaderModuleCreateInfo shaderModuleCI
	{ 
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 
		.codeSize = spirv->getBufferSize(), 
		.pCode = (uint32_t*)spirv->getBufferPointer() 
	};

	chk(vkCreateShaderModule(g_Device, &shaderModuleCI, nullptr, &g_ShaderModule));

	CreateDescriptorSetLayout();
	CreateDescriptorSetPool();
	AllocateDescriptorSet();

	// Update Descriptor Set
	VkWriteDescriptorSet writeDescSet
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = g_DescriptorSetTex,
		.dstBinding = 0,
		.descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = textureDescriptors.data()
	};
	vkUpdateDescriptorSets(g_Device, 1, &writeDescSet, 0, nullptr);

	CreateGraphicsPipeline();

	// Render loop
	uint64_t lastTime{ SDL_GetTicks() };

	bool bQuit{ false };
	while (!bQuit)
	{
		// Sync
		chk(vkWaitForFences(g_Device, 1, &g_Fences[g_FrameIndex], true, UINT64_MAX));
		chk(vkResetFences(g_Device, 1, &g_Fences[g_FrameIndex]));
		chkSwapchain(vkAcquireNextImageKHR(g_Device, g_Swapchain, UINT64_MAX, g_PresentSemaphores[g_FrameIndex], VK_NULL_HANDLE, &g_ImageIndex));
		
		// Update shader data
		g_ShaderData.projection = glm::perspective(glm::radians(45.0f), (float)g_WindowSize.x / (float)g_WindowSize.y, 0.1f, 32.0f);
		g_ShaderData.view = glm::translate(glm::mat4(1.0f), g_CamPos);
		for (auto i = 0; i < 3; i++)
		{
			auto instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
			g_ShaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(g_ObjectRotations[i]));
		}
		memcpy(g_ShaderDataBuffers[g_FrameIndex].mapped, &g_ShaderData, sizeof(ShaderData));

		// Build command buffer
		auto cb = g_CommandBuffers[g_FrameIndex];
		chk(vkResetCommandBuffer(cb, 0));
		
		VkCommandBufferBeginInfo cbBI
		{ 
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
		};
		chk(vkBeginCommandBuffer(cb, &cbBI));

		std::array<VkImageMemoryBarrier2, 2> outputBarriers
		{
			VkImageMemoryBarrier2
			{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = g_SwapchainImages[g_ImageIndex],
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			},
			VkImageMemoryBarrier2
			{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = g_DepthImage,
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
			}
		};
		VkDependencyInfo barrierDependencyInfo
		{ 
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 
			.imageMemoryBarrierCount = 2, 
			.pImageMemoryBarriers = outputBarriers.data() 
		};
		vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);
		
		VkRenderingAttachmentInfo colorAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = g_SwapchainImageViews[g_ImageIndex],
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue
			{
				.color{ 0.0f, 0.0f, 0.0f, 1.0f }
			}
		};
		VkRenderingAttachmentInfo depthAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = g_DepthImageView,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = 
			{
				.depthStencil = {1.0f,  0}
			}
		};
		VkRenderingInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea{.extent{.width = static_cast<uint32_t>(g_WindowSize.x), .height = static_cast<uint32_t>(g_WindowSize.y) }},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp
		{ 
			.width = static_cast<float>(g_WindowSize.x),
			.height = static_cast<float>(g_WindowSize.y),
			.minDepth = 0.0f, 
			.maxDepth = 1.0f 
		};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor
		{ 
			.extent
			{
				.width = static_cast<uint32_t>(g_WindowSize.x), 
				.height = static_cast<uint32_t>(g_WindowSize.y) 
			} 
		};
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipeline);
		vkCmdSetScissor(cb, 0, 1, &scissor);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_PipelineLayout, 0, 1, &g_DescriptorSetTex, 0, nullptr);
		
		vkCmdBindVertexBuffers(cb, 0, 1, &g_VertexBuffer.handle, &g_VertexBuffer.vertexOffset);
		vkCmdBindIndexBuffer(cb, g_VertexBuffer.handle, g_VertexBuffer.vertexSize, VK_INDEX_TYPE_UINT16);
		vkCmdPushConstants(cb, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &g_ShaderDataBuffers[g_FrameIndex].deviceAddress);
		
		uint32_t indexCount = 0;
		switch (g_VertexBuffer.indexType)
		{
		case VK_INDEX_TYPE_UINT16:
			indexCount = (g_VertexBuffer.indexSize) / sizeof(uint16_t);
			break;
		case VK_INDEX_TYPE_UINT32:
			indexCount = (g_VertexBuffer.indexSize) / sizeof(uint32_t);
			break;
		default:
			break;
		}
		
		vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0);
		vkCmdEndRendering(cb);

		VkImageMemoryBarrier2 barrierPresent
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = g_SwapchainImages[g_ImageIndex],
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, 
				.levelCount = 1, 
				.layerCount = 1 
			}
		};
		VkDependencyInfo barrierPresentDependencyInfo
		{ 
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 
			.imageMemoryBarrierCount = 1, 
			.pImageMemoryBarriers = &barrierPresent 
		};
		vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);
		chk(vkEndCommandBuffer(cb));

		// Submit to graphics queue
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &g_PresentSemaphores[g_FrameIndex],
			.pWaitDstStageMask = &waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cb,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &g_RenderSemaphores[g_ImageIndex],
		};
		chk(vkQueueSubmit(g_Queue, 1, &submitInfo, g_Fences[g_FrameIndex]));
		g_FrameIndex = (g_FrameIndex + 1) % g_MaxFramesInFlight;
		
		VkPresentInfoKHR presentInfo
		{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &g_RenderSemaphores[g_ImageIndex],
			.swapchainCount = 1,
			.pSwapchains = &g_Swapchain,
			.pImageIndices = &g_ImageIndex
		};
		chkSwapchain(vkQueuePresentKHR(g_Queue, &presentInfo));

		// Event polling
		float elapsedTime{ (SDL_GetTicks() - lastTime) / 1000.0f };
		lastTime = SDL_GetTicks();
		for (SDL_Event event; SDL_PollEvent(&event);)
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				bQuit = true;
				break;
			}
			if (event.type == SDL_EVENT_MOUSE_MOTION)
			{
				if (event.button.button == SDL_BUTTON_LEFT)
				{
					g_ObjectRotations[g_ShaderData.selected].x -= (float)event.motion.yrel * elapsedTime;
					g_ObjectRotations[g_ShaderData.selected].y += (float)event.motion.xrel * elapsedTime;
				}
			}
			if (event.type == SDL_EVENT_MOUSE_WHEEL)
			{
				g_CamPos.z += (float)event.wheel.y * elapsedTime * 10.0f;
			}
			if (event.type == SDL_EVENT_KEY_DOWN)
			{
				if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS)
				{
					g_ShaderData.selected = (g_ShaderData.selected < 2) ? g_ShaderData.selected + 1 : 0;
				}
				if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
				{
					g_ShaderData.selected = (g_ShaderData.selected > 0) ? g_ShaderData.selected - 1 : 2;
				}
			}
			// Window resize
			if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				g_bUpdateSwapchain = true;
			}
		}

		if (g_bUpdateSwapchain)
		{
			CreateSwapchain(g_Swapchain);
		}
	}
	TearDown();
}

void CreateInstance()
{
	// Instance
	VkApplicationInfo appInfo{ .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "How to Vulkan", .apiVersion = VK_API_VERSION_1_3 };
	uint32_t instanceExtensionsCount{ 0 };
	char const* const* instanceExtensions{ SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount) };
	VkInstanceCreateInfo instanceCI{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = instanceExtensionsCount,
		.ppEnabledExtensionNames = instanceExtensions,
	};
	chk(vkCreateInstance(&instanceCI, nullptr, &g_Instance));
	volkLoadInstance(g_Instance);
}

void CreateDevice(int deviceIndex)
{
	// Physical device
	uint32_t deviceCount{ 0 };
	chk(vkEnumeratePhysicalDevices(g_Instance, &deviceCount, nullptr));
	std::vector<VkPhysicalDevice> devices(deviceCount);
	chk(vkEnumeratePhysicalDevices(g_Instance, &deviceCount, devices.data()));

	assert(deviceIndex < deviceCount);

	g_PhysicalDevice = std::move(devices[deviceIndex]);

	VkPhysicalDeviceProperties2 deviceProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	vkGetPhysicalDeviceProperties2(g_PhysicalDevice, &deviceProperties);
	std::cout << "Selected device: " << deviceProperties.properties.deviceName << "\n";

	// Find a queue family for graphics
	uint32_t queueFamilyCount{ 0 };
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

	for (size_t i = 0; i < queueFamilies.size(); i++)
	{
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			g_QueueFamilyIndex = i;
			break;
		}
	}

	// Logical device
	const float qfpriorities{ 1.0f };
	VkDeviceQueueCreateInfo queueCI
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = g_QueueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = &qfpriorities
	};
	VkPhysicalDeviceVulkan12Features enabledVk12Features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.descriptorIndexing = true,
		.descriptorBindingVariableDescriptorCount = true,
		.runtimeDescriptorArray = true,
		.bufferDeviceAddress = true
	};
	VkPhysicalDeviceVulkan13Features enabledVk13Features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &enabledVk12Features,
		.synchronization2 = true,
		.dynamicRendering = true
	};
	const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	const VkPhysicalDeviceFeatures enabledVk10Features{ .samplerAnisotropy = VK_TRUE };

	VkDeviceCreateInfo deviceCI{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &enabledVk13Features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCI,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = &enabledVk10Features
	};
	chk(vkCreateDevice(g_PhysicalDevice, &deviceCI, nullptr, &g_Device));
	vkGetDeviceQueue(g_Device, g_QueueFamilyIndex, 0, &g_Queue);
}

void CreateAllocator()
{
	// VMA
	VmaVulkanFunctions vkFunctions
	{
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
		.vkCreateImage = vkCreateImage
	};
	VmaAllocatorCreateInfo allocatorCI
	{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = g_PhysicalDevice,
		.device = g_Device,
		.pVulkanFunctions = &vkFunctions,
		.instance = g_Instance
	};
	chk(vmaCreateAllocator(&allocatorCI, &g_Allocator));
}

void CreateSurface()
{
	// Window and surface
	g_SDLWindow = SDL_CreateWindow("How to Vulkan", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	assert(g_SDLWindow);

	chk(SDL_Vulkan_CreateSurface(g_SDLWindow, g_Instance, nullptr, &g_Surface));
	chk(SDL_GetWindowSize(g_SDLWindow, &g_WindowSize.x, &g_WindowSize.y));
	chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PhysicalDevice, g_Surface, &g_SurfaceCaps));
}

void CreateSwapchain(VkSwapchainKHR oldSwapchain)
{
	// Swap chain
	VkSwapchainCreateInfoKHR swapchainCI
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = g_Surface,
		.minImageCount = g_SurfaceCaps.minImageCount,
		.imageFormat = g_kSwapchainImageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent
		{
			.width = static_cast<uint32_t>(g_WindowSize.x),
			.height = static_cast<uint32_t>(g_WindowSize.y)
		},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.oldSwapchain = oldSwapchain
	};
	chk(vkCreateSwapchainKHR(g_Device, &swapchainCI, nullptr, &g_Swapchain));

	// NOTICE: Swapchain의 Image는 직접 해제하지 않으므로 oldSwapchain 여부에 상관하지 않습니다.
	chk(vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &g_SwapchainImageCount, nullptr));
	g_SwapchainImages.resize(g_SwapchainImageCount);
	chk(vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &g_SwapchainImageCount, g_SwapchainImages.data()));

	g_SwapchainImageViews.resize(g_SwapchainImageCount);
	for (auto i = 0; i < g_SwapchainImageCount; i++)
	{
		VkImageViewCreateInfo viewCI
		{ 
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
			.image = g_SwapchainImages[i], 
			.viewType = VK_IMAGE_VIEW_TYPE_2D, 
			.format = g_kSwapchainImageFormat, 
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, 
				.levelCount = 1, 
				.layerCount = 1 
			} 
		};
		chk(vkCreateImageView(g_Device, &viewCI, nullptr, &g_SwapchainImageViews[i]));
	}

	if (oldSwapchain)
	{
		vkDestroySwapchainKHR(g_Device, oldSwapchain, nullptr);
		// oldSwapchain이 존재한다면 재생성인 경우이므로 Depth Image도 재생성합니다.
		CreateDepthImage(g_WindowSize.x, g_WindowSize.y);
	}
}

void CreateDepthImage(uint32_t width, uint32_t height)
{
	// Depth attachment
	if (g_DepthImageAllocation)
	{
		assert(g_DepthImage != VK_NULL_HANDLE);
		vmaDestroyImage(g_Allocator, g_DepthImage, g_DepthImageAllocation);
	
		assert(g_DepthImageView != VK_NULL_HANDLE);
		vkDestroyImageView(g_Device, g_DepthImageView, nullptr);
	}

	// Depth attachment
	std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };

	for (VkFormat& format : depthFormatList)
	{
		VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
		vkGetPhysicalDeviceFormatProperties2(g_PhysicalDevice, format, &formatProperties);
		if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			g_DepthImageFormat = format;
			break;
		}
	}
	assert(g_DepthImageFormat != VK_FORMAT_UNDEFINED);

	VkImageCreateInfo depthImageCI{
	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	.imageType = VK_IMAGE_TYPE_2D,
	.format = g_DepthImageFormat,
	.extent = 
	{
		.width = width, 
		.height = height, 
		.depth = 1
	},
	.mipLevels = 1,
	.arrayLayers = 1,
	.samples = VK_SAMPLE_COUNT_1_BIT,
	.tiling = VK_IMAGE_TILING_OPTIMAL,
	.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo allocCI
	{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	chk(vmaCreateImage(g_Allocator, &depthImageCI, &allocCI, &g_DepthImage, &g_DepthImageAllocation, nullptr));
	VkImageViewCreateInfo depthViewCI
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = g_DepthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = g_DepthImageFormat,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};
	chk(vkCreateImageView(g_Device, &depthViewCI, nullptr, &g_DepthImageView));
}

void LoadMeshAndCreateVertexDatas()
{
	// Mesh data
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	chk(tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/suzanne.obj"));
	VkDeviceSize g_IndexCount{ shapes[0].mesh.indices.size() };
	std::vector<Vertex> vertices{};
	std::vector<uint16_t> indices{};

	// Load vertex and index data
	for (auto& index : shapes[0].mesh.indices)
	{
		Vertex v
		{
			.pos = { attrib.vertices[index.vertex_index * 3], -attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2] },
			.normal = { attrib.normals[index.normal_index * 3], -attrib.normals[index.normal_index * 3 + 1], attrib.normals[index.normal_index * 3 + 2] },
			.uv = { attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2 + 1] }
		};
		vertices.push_back(v);
		indices.push_back(indices.size());
	}
	VkDeviceSize vBufSize{ sizeof(Vertex) * vertices.size() };
	VkDeviceSize iBufSize{ sizeof(uint16_t) * indices.size() };
	VkBufferCreateInfo bufferCI
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = vBufSize + iBufSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	};
	VmaAllocationCreateInfo bufferAllocCI
	{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
		VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | 
		VMA_ALLOCATION_CREATE_MAPPED_BIT,

		.usage = VMA_MEMORY_USAGE_AUTO
	};
	g_VertexBuffer.size = bufferCI.size;
	g_VertexBuffer.vertexOffset = 0;
	g_VertexBuffer.vertexSize = vBufSize;
	g_VertexBuffer.indexOffset = vBufSize;
	g_VertexBuffer.indexSize = iBufSize;
	g_VertexBuffer.indexType = VK_INDEX_TYPE_UINT16;

	chk(vmaCreateBuffer(g_Allocator, &bufferCI, &bufferAllocCI, &g_VertexBuffer.handle, &g_VertexBuferAllocation, nullptr));

	// 데이터 매핑
	void* bufferPtr{ nullptr };
	chk(vmaMapMemory(g_Allocator, g_VertexBuferAllocation, &bufferPtr));
	memcpy(bufferPtr, vertices.data(), vBufSize);
	memcpy(((char*)bufferPtr) + vBufSize, indices.data(), iBufSize);
	vmaUnmapMemory(g_Allocator, g_VertexBuferAllocation);
}

void CreateShaderDataBuffers()
{
	// Shader data buffers
	for (auto i = 0; i < g_MaxFramesInFlight; i++)
	{
		VkBufferCreateInfo uBufferCI
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(ShaderData),
			.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		};

		VmaAllocationCreateInfo uBufferAllocCI
		{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
				VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | 
				VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		chk(vmaCreateBuffer(g_Allocator, &uBufferCI, &uBufferAllocCI, &g_ShaderDataBuffers[i].buffer, &g_ShaderDataBuffers[i].allocation, nullptr));
		chk(vmaMapMemory(g_Allocator, g_ShaderDataBuffers[i].allocation, &g_ShaderDataBuffers[i].mapped));
		VkBufferDeviceAddressInfo uBufferBdaInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = g_ShaderDataBuffers[i].buffer
		};
		g_ShaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(g_Device, &uBufferBdaInfo);
	}
}

void InitializeTextures(std::vector<VkDescriptorImageInfo>& textureDesc)
{
	// Texture images
	for (auto i = 0; i < g_Textures.size(); i++)
	{
		ktxTexture* ktxTexture{ nullptr };
		std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
		ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		VkImageCreateInfo texImgCI{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = ktxTexture_GetVkFormat(ktxTexture),
			.extent = {.width = ktxTexture->baseWidth, .height = ktxTexture->baseHeight, .depth = 1 },
			.mipLevels = ktxTexture->numLevels,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};

		VmaAllocationCreateInfo texImageAllocCI
		{
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		chk(vmaCreateImage(g_Allocator, &texImgCI, &texImageAllocCI, &g_Textures[i].image, &g_Textures[i].allocation, nullptr));

		VkImageViewCreateInfo texVewCI
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = g_Textures[i].image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = texImgCI.format,
			.subresourceRange =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = ktxTexture->numLevels,
				.layerCount = 1
			}
		};
		chk(vkCreateImageView(g_Device, &texVewCI, nullptr, &g_Textures[i].view));

		// Upload
		VkBuffer imgSrcBuffer{};
		VmaAllocation imgSrcAllocation{};
		VkBufferCreateInfo imgSrcBufferCI
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = (uint32_t)ktxTexture->dataSize,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		};
		VmaAllocationCreateInfo imgSrcAllocCI
		{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO
		};

		chk(vmaCreateBuffer(g_Allocator, &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation, nullptr));
		void* imgSrcBufferPtr{ nullptr };
		chk(vmaMapMemory(g_Allocator, imgSrcAllocation, &imgSrcBufferPtr));
		memcpy(imgSrcBufferPtr, ktxTexture->pData, ktxTexture->dataSize);

		VkFenceCreateInfo fenceOneTimeCI
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
		};
		VkFence fenceOneTime{};
		chk(vkCreateFence(g_Device, &fenceOneTimeCI, nullptr, &fenceOneTime));

		VkCommandBuffer cbOneTime{};
		VkCommandBufferAllocateInfo cbOneTimeAI
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = g_CommandPool,
			.commandBufferCount = 1
		};
		chk(vkAllocateCommandBuffers(g_Device, &cbOneTimeAI, &cbOneTime));

		VkCommandBufferBeginInfo cbOneTimeBI
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		chk(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));

		VkImageMemoryBarrier2 barrierTexImage{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = g_Textures[i].image,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		VkDependencyInfo barrierTexInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrierTexImage };
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);

		std::vector<VkBufferImageCopy> copyRegions{};
		for (auto j = 0; j < ktxTexture->numLevels; j++)
		{
			ktx_size_t mipOffset{ 0 };
			KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
			copyRegions.push_back({
				.bufferOffset = mipOffset,
				.imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
				.imageExtent{.width = ktxTexture->baseWidth >> j, .height = ktxTexture->baseHeight >> j, .depth = 1 },
				});
		}
		vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, g_Textures[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

		VkImageMemoryBarrier2 barrierTexRead{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			.image = g_Textures[i].image,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		chk(vkEndCommandBuffer(cbOneTime));

		VkSubmitInfo oneTimeSI
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cbOneTime
		};
		chk(vkQueueSubmit(g_Queue, 1, &oneTimeSI, fenceOneTime));

		chk(vkWaitForFences(g_Device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));
		vkDestroyFence(g_Device, fenceOneTime, nullptr);
		vmaUnmapMemory(g_Allocator, imgSrcAllocation);
		vmaDestroyBuffer(g_Allocator, imgSrcBuffer, imgSrcAllocation);

		// Sampler
		VkSamplerCreateInfo samplerCI{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = 8.0f,
			.maxLod = (float)ktxTexture->numLevels,
		};
		chk(vkCreateSampler(g_Device, &samplerCI, nullptr, &g_Textures[i].sampler));
		ktxTexture_Destroy(ktxTexture);
		textureDesc.push_back(
			{
				.sampler = g_Textures[i].sampler,
				.imageView = g_Textures[i].view,
				.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
			});
	}
}

void CreateSyncObjects()
{
	// Sync objects
	VkSemaphoreCreateInfo semaphoreCI
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fenceCI
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	for (auto i = 0; i < g_MaxFramesInFlight; i++)
	{
		chk(vkCreateFence(g_Device, &fenceCI, nullptr, &g_Fences[i]));
		chk(vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &g_PresentSemaphores[i]));
	}
	g_RenderSemaphores.resize(g_SwapchainImages.size());
	for (auto& semaphore : g_RenderSemaphores)
	{
		chk(vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &semaphore));
	}
}

void CreateDescriptorSetLayout()
{
	// Descriptor (indexing)
	VkDescriptorBindingFlags descVariableFlag
	{
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
	};
	VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 1,
		.pBindingFlags = &descVariableFlag
	};
	VkDescriptorSetLayoutBinding descLayoutBindingTex
	{
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = static_cast<uint32_t>(g_Textures.size()),
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	VkDescriptorSetLayoutCreateInfo descLayoutTexCI
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descBindingFlags,
		.bindingCount = 1,
		.pBindings = &descLayoutBindingTex
	};
	chk(vkCreateDescriptorSetLayout(g_Device, &descLayoutTexCI, nullptr, &g_DescriptorSetLayoutTex));
}

void CreateDescriptorSetPool()
{
	VkDescriptorPoolSize poolSize
	{
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = static_cast<uint32_t>(g_Textures.size())
	};
	VkDescriptorPoolCreateInfo descPoolCI
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSize
	};
	chk(vkCreateDescriptorPool(g_Device, &descPoolCI, nullptr, &g_DescriptorPool));

}

void AllocateDescriptorSet()
{
	uint32_t variableDescCount{ static_cast<uint32_t>(g_Textures.size()) };
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
		.descriptorSetCount = 1,
		.pDescriptorCounts = &variableDescCount
	};
	VkDescriptorSetAllocateInfo texDescSetAlloc
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = &variableDescCountAI,
		.descriptorPool = g_DescriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &g_DescriptorSetLayoutTex
	};
	chk(vkAllocateDescriptorSets(g_Device, &texDescSetAlloc, &g_DescriptorSetTex));
}

void CreateGraphicsPipeline()
{
	VkPushConstantRange pushConstantRange
	{ 
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, 
		.size = sizeof(VkDeviceAddress) 
	};
	VkPipelineLayoutCreateInfo pipelineLayoutCI
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 
		.setLayoutCount = 1, 
		.pSetLayouts = &g_DescriptorSetLayoutTex, 
		.pushConstantRangeCount = 1, 
		.pPushConstantRanges = &pushConstantRange 
	};
	chk(vkCreatePipelineLayout(g_Device, &pipelineLayoutCI, nullptr, &g_PipelineLayout));
	
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = g_ShaderModule, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = g_ShaderModule, .pName = "main" }
	};
	
	VkVertexInputBindingDescription vertexBinding
	{ 
		.binding = 0, 
		.stride = sizeof(Vertex), 
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX 
	};
	std::vector<VkVertexInputAttributeDescription> vertexAttributes{
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
		{.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
	};
	VkPipelineVertexInputStateCreateInfo vertexInputState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertexBinding,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
		.pVertexAttributeDescriptions = vertexAttributes.data(),
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, 
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 
	};
	
	std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates.data() 
	};
	
	VkPipelineViewportStateCreateInfo viewportState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 
		.viewportCount = 1, 
		.scissorCount = 1 
	};
	
	VkPipelineRasterizationStateCreateInfo rasterizationState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, 
		.lineWidth = 1.0f 
	};
	
	VkPipelineMultisampleStateCreateInfo multisampleState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT 
	};
	
	VkPipelineDepthStencilStateCreateInfo depthStencilState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, 
		.depthTestEnable = VK_TRUE, 
		.depthWriteEnable = VK_TRUE, 
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL 
	};
	
	VkPipelineColorBlendAttachmentState blendAttachment
	{ 
		.colorWriteMask = 0xF // 0b00001111(____RGBA)
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 
		.attachmentCount = 1, 
		.pAttachments = &blendAttachment 
	};
	
	VkPipelineRenderingCreateInfo renderingCI
	{ 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, 
		.colorAttachmentCount = 1, 
		.pColorAttachmentFormats = &g_kSwapchainImageFormat, 
		.depthAttachmentFormat = g_DepthImageFormat 
	};
	
	VkGraphicsPipelineCreateInfo pipelineCI
	{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCI,
		.stageCount = 2,
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputState,
		.pInputAssemblyState = &inputAssemblyState,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizationState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dynamicState,
		.layout = g_PipelineLayout
	};
	chk(vkCreateGraphicsPipelines(g_Device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &g_Pipeline));
}

void TearDown()
{
	chk(vkDeviceWaitIdle(g_Device));
	for (auto i = 0; i < g_MaxFramesInFlight; i++)
	{
		vkDestroyFence(g_Device, g_Fences[i], nullptr);
		vkDestroySemaphore(g_Device, g_PresentSemaphores[i], nullptr);
		vmaUnmapMemory(g_Allocator, g_ShaderDataBuffers[i].allocation);
		vmaDestroyBuffer(g_Allocator, g_ShaderDataBuffers[i].buffer, g_ShaderDataBuffers[i].allocation);
	}
	for (auto i = 0; i < g_RenderSemaphores.size(); i++)
	{
		vkDestroySemaphore(g_Device, g_RenderSemaphores[i], nullptr);
	}
	vmaDestroyImage(g_Allocator, g_DepthImage, g_DepthImageAllocation);
	vkDestroyImageView(g_Device, g_DepthImageView, nullptr);
	for (auto i = 0; i < g_SwapchainImageViews.size(); i++)
	{
		vkDestroyImageView(g_Device, g_SwapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(g_Allocator, g_VertexBuffer.handle, g_VertexBuferAllocation);
	for (auto i = 0; i < g_Textures.size(); i++)
	{
		vkDestroyImageView(g_Device, g_Textures[i].view, nullptr);
		vkDestroySampler(g_Device, g_Textures[i].sampler, nullptr);
		vmaDestroyImage(g_Allocator, g_Textures[i].image, g_Textures[i].allocation);
	}
	vkDestroyDescriptorSetLayout(g_Device, g_DescriptorSetLayoutTex, nullptr);
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr);
	vkDestroyPipelineLayout(g_Device, g_PipelineLayout, nullptr);
	vkDestroyPipeline(g_Device, g_Pipeline, nullptr);
	vkDestroySwapchainKHR(g_Device, g_Swapchain, nullptr);
	vkDestroySurfaceKHR(g_Instance, g_Surface, nullptr);
	vkDestroyCommandPool(g_Device, g_CommandPool, nullptr);
	vkDestroyShaderModule(g_Device, g_ShaderModule, nullptr);
	vmaDestroyAllocator(g_Allocator);
	SDL_DestroyWindow(g_SDLWindow);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();
	vkDestroyDevice(g_Device, nullptr);
	vkDestroyInstance(g_Instance, nullptr);
}