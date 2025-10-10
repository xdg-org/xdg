// vulkan_probe.h
#pragma once
#include <iostream>
#include <vulkan/vulkan.h>

inline bool system_has_vk_device(uint32_t min_instance = VK_API_VERSION_1_1) {
  uint32_t loaderVer = VK_API_VERSION_1_0;
  vkEnumerateInstanceVersion(&loaderVer);

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
}