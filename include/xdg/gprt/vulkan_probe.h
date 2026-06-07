#pragma once

#ifdef XDG_ENABLE_GPRT
#include "xdg/error.h"

#include <cstring>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

inline std::vector<VkExtensionProperties>
get_vk_device_extensions(VkPhysicalDevice device)
{
  uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
    return {};

  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS)
    return {};

  return extensions;
}

inline bool check_if_extension_available(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
  for (const auto& extension : extensions) {
    if (std::strcmp(extension.extensionName, name) == 0) return true;
  }
  return false;
}

inline bool system_has_vk_device()
{
  VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
  app.pApplicationName = "vk-probe";
  app.applicationVersion = 1;
  app.pEngineName = "probe";
  app.engineVersion = 1;
  app.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.pApplicationInfo = &app;

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
    warning("Could not create Vulkan instance; GPRT ray tracer unavailable.");
    return false;
  }

  uint32_t devCount = 0;
  if (vkEnumeratePhysicalDevices(instance, &devCount, nullptr) != VK_SUCCESS || devCount == 0) {
    vkDestroyInstance(instance, nullptr);
    warning("No Vulkan physical devices found; GPRT ray tracer unavailable.");
    return false;
  }

  std::vector<VkPhysicalDevice> devices(devCount);
  if (vkEnumeratePhysicalDevices(instance, &devCount, devices.data()) != VK_SUCCESS) {
    vkDestroyInstance(instance, nullptr);
    warning("Could not enumerate Vulkan physical devices; GPRT ray tracer unavailable.");
    return false;
  }

  std::string missing;
  for (auto device : devices) {
    auto extensions = get_vk_device_extensions(device);
    bool accel_ext = check_if_extension_available(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    bool rt_pipeline_ext = check_if_extension_available(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

    // these are the two main extensions we need for GPRT so if both are present we can stop checking other devices
    if (accel_ext && rt_pipeline_ext) {
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(device, &properties);
      write_message("Found Vulkan ray tracing capable device '{}'.", properties.deviceName);
      vkDestroyInstance(instance, nullptr);
      return true;
    }

    missing.clear();
    if (!accel_ext) missing += VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
    if (!rt_pipeline_ext) {
      if (!missing.empty()) missing += ", ";
      missing += VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
    }
  }

  vkDestroyInstance(instance, nullptr);
  warning("No Vulkan device with the required GPRT extensions found. Missing extensions: " +
          missing);
  return false;
}

#endif
