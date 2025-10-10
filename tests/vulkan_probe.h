#pragma once

#ifdef XDG_ENABLE_GPRT
#include <iostream>
#include <vulkan/vulkan.h>

inline bool system_has_vk_device(uint32_t min_instance = VK_API_VERSION_1_1) {
  uint32_t loaderVer = VK_API_VERSION_1_0;
  vkEnumerateInstanceVersion(&loaderVer);

  // Create VK instance
  VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
  app.pApplicationName = "vk-probe";
  app.applicationVersion = 1;
  app.pEngineName = "probe";
  app.engineVersion = 1;
  app.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.pApplicationInfo = &app;

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance)
    return false;

  // Look for at least one physical device
  uint32_t devCount = 0;
  VkResult r = vkEnumeratePhysicalDevices(instance, &devCount, nullptr);

  // Clean up the instance before returning
  vkDestroyInstance(instance, nullptr);

  // Check for errors and non-zero device count
  if (r != VK_SUCCESS || devCount == 0) {
    return false;
  }

  return true; // VK device detected
}

#endif
