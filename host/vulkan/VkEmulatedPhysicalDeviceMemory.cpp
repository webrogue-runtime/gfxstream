// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "VkEmulatedPhysicalDeviceMemory.h"

#include <algorithm>
#include <limits>

#include "gfxstream/common/logging.h"

#if 1
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#endif

namespace gfxstream {
namespace vk {
namespace {

#ifdef max
#undef max
#endif
static constexpr const uint32_t kInvalidMemoryTypeIndex = std::numeric_limits<uint32_t>::max();

}  // namespace

EmulatedPhysicalDeviceMemoryProperties::EmulatedPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
    VulkanDispatch* vk,
    const VkPhysicalDeviceMemoryProperties& hostMemoryProperties,
    const uint32_t hostColorBufferMemoryTypeIndex, const gfxstream::host::FeatureSet& features) {
    // Start with the original host memory properties:
    mHostMemoryProperties = hostMemoryProperties;
    mGuestMemoryProperties = hostMemoryProperties;
    std::fill_n(mGuestToHostMemoryTypeIndexMap, VK_MAX_MEMORY_TYPES, kInvalidMemoryTypeIndex);
    std::fill_n(mHostToGuestMemoryTypeIndexMap, VK_MAX_MEMORY_TYPES, kInvalidMemoryTypeIndex);
    for (uint32_t i = 0; i < mHostMemoryProperties.memoryTypeCount; i++) {
        mGuestToHostMemoryTypeIndexMap[i] = i;
        mHostToGuestMemoryTypeIndexMap[i] = i;
    }
    mGuestColorBufferMemoryTypeIndex = hostColorBufferMemoryTypeIndex;

    // Hide any bogus heap sizes from bad drivers with a reasonable default that will not
    // break the bank on 32-bit userspaces.
    static constexpr VkDeviceSize kMaxSafeHeapSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    for (uint32_t i = 0; i < mHostMemoryProperties.memoryHeapCount; i++) {
        if (mGuestMemoryProperties.memoryHeaps[i].size > kMaxSafeHeapSize) {
            mGuestMemoryProperties.memoryHeaps[i].size = kMaxSafeHeapSize;
        }
    }

    // If enabled, hide non device memory types from the guest.
    // (useful to work around a bug where KVM can't map TTM memory).
    if (features.VulkanAllocateDeviceMemoryOnly.enabled) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            auto guestMemoryProperties = mGuestMemoryProperties.memoryTypes[i].propertyFlags;
            if (!(guestMemoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags = 0;
            }
        }
    }

    // Coherent memory in the guest requires one of these features:
    if (!features.GlDirectMem.enabled && !features.VirtioGpuNext.enabled) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            mGuestMemoryProperties.memoryTypes[i].propertyFlags =
                mGuestMemoryProperties.memoryTypes[i].propertyFlags &
                ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
    }

    // Let cached memory pretend as coherent on the guest side.
    if (features.VulkanDisableCoherentMemoryAndEmulate.enabled) {
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            if (mGuestMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            } else {
                mGuestMemoryProperties.memoryTypes[i].propertyFlags &= ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            }
        }
    }

    if (features.VulkanEnsureCachedCoherentMemoryAvailable.enabled) {
        /* Some app layers (i.e. Angle) require *some* coherent-cached memory to be
         *  available. To ensure compatiblity these guest layers, when coherent-cached
         *  memory type is unavailable, append the cached bit to the first coherent
         *  memory type available. Note that in this scenario, there is no potential
         *  functional downside to marking one of the host-coherent as cached, aside
         *  from the guest layer believing there will be some performance benefit to
         *  using this particular memory.
         */
        bool hasCoherentCached = false;
        uint32_t firstCoherent = VK_MAX_MEMORY_TYPES;
        for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
            const VkMemoryPropertyFlags flags = mGuestMemoryProperties.memoryTypes[i].propertyFlags;
            const bool coherent = flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            const bool cached = flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            if (coherent) {
                if (firstCoherent == VK_MAX_MEMORY_TYPES) {
                    firstCoherent = i;
                }
                if (cached) {
                    hasCoherentCached = true;
                }
            }
        }

        if (!hasCoherentCached) {
            if (firstCoherent == VK_MAX_MEMORY_TYPES) {
                GFXSTREAM_FATAL(
                    "Unexpected memoryTypes error -- no available host-coherent memory.");
            }
            mGuestMemoryProperties.memoryTypes[firstCoherent].propertyFlags |=
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
    }

    // If enabled, reserve an additional memory type for AHB backed buffers and images
    // so that the host can control its memory properties. This ensures that the guest
    // only sees `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` and will not try to map the
    // memory.
    if (features.VulkanUseDedicatedAhbMemoryType.enabled) {
        if (mGuestMemoryProperties.memoryTypeCount == VK_MAX_MEMORY_TYPES) {
            GFXSTREAM_FATAL("Unable to create emulated AHB memory type because VK_MAX_MEMORY_TYPES "
                            "already in use.");
        }

        uint32_t ahbMemoryTypeIndex = mGuestMemoryProperties.memoryTypeCount;
        ++mGuestMemoryProperties.memoryTypeCount;

        VkMemoryType& ahbMemoryType = mGuestMemoryProperties.memoryTypes[ahbMemoryTypeIndex];
        ahbMemoryType.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        ahbMemoryType.heapIndex =
            mHostMemoryProperties.memoryTypes[hostColorBufferMemoryTypeIndex].heapIndex;

        mGuestToHostMemoryTypeIndexMap[ahbMemoryTypeIndex] = hostColorBufferMemoryTypeIndex;

        mGuestColorBufferMemoryTypeIndex = ahbMemoryTypeIndex;
    }
#if 1
    if(vk) {
        VkMemoryHostPointerPropertiesEXT memoryHostPointerProperties = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
            .pNext = NULL,
            .memoryTypeBits = 0,
        };
        VkResult ret = VK_SUCCESS;
        char extensionName[] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
        char* extensionNames[] = { extensionName };
        float queuePriority = 0.5;
        VkDeviceQueueCreateInfo queueCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
        VkDeviceCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            // enabledLayerCount is deprecated and should not be used
            .enabledLayerCount = 0,
            // ppEnabledLayerNames is deprecated and should not be used
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = 1,
            .ppEnabledExtensionNames = extensionNames,
            .pEnabledFeatures = nullptr,
        };
        VkDevice device;
        ret = vk->vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);

        if(ret == VK_SUCCESS) {
            auto vkGetMemoryHostPointerPropertiesEXT = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(vk->vkGetDeviceProcAddr(device, "vkGetMemoryHostPointerPropertiesEXT"));
            if(vkGetMemoryHostPointerPropertiesEXT) {
                const size_t alloc_size = 16 * 1024;
                void *mappedPtr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
                ret = vk->vkGetMemoryHostPointerPropertiesEXT(
                    device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, mappedPtr,
                    &memoryHostPointerProperties
                );
                munmap(mappedPtr, alloc_size);

                if (ret == VK_SUCCESS) {
                    for (uint32_t i = 0; i < mGuestMemoryProperties.memoryTypeCount; i++) {
                        bool supportsHostImport = memoryHostPointerProperties.memoryTypeBits & (1 << i);
                        if (!supportsHostImport) {
                            mGuestMemoryProperties.memoryTypes[i].propertyFlags &= ~(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                        }
                    }
                }
            }
            vk->vkDestroyDevice(device, nullptr);
        }
    }
#endif // 1
}

std::optional<EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo>
EmulatedPhysicalDeviceMemoryProperties::getHostMemoryInfoFromHostMemoryTypeIndex(
    uint32_t hostMemoryTypeIndex) const {
    if (hostMemoryTypeIndex >= mHostMemoryProperties.memoryTypeCount) {
        return std::nullopt;
    }

    return HostMemoryInfo{
        .index = hostMemoryTypeIndex,
        .memoryType = mHostMemoryProperties.memoryTypes[hostMemoryTypeIndex],
    };
}

std::optional<EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo>
EmulatedPhysicalDeviceMemoryProperties::getHostMemoryInfoFromGuestMemoryTypeIndex(
    uint32_t guestMemoryTypeIndex) const {
    if (guestMemoryTypeIndex >= mGuestMemoryProperties.memoryTypeCount) {
        return std::nullopt;
    }

    uint32_t hostMemoryTypeIndex = mGuestToHostMemoryTypeIndexMap[guestMemoryTypeIndex];
    if (hostMemoryTypeIndex == kInvalidMemoryTypeIndex) {
        return std::nullopt;
    }

    return getHostMemoryInfoFromHostMemoryTypeIndex(hostMemoryTypeIndex);
}

void EmulatedPhysicalDeviceMemoryProperties::transformToGuestMemoryRequirements(
    VkMemoryRequirements* memoryRequirements) const {
    uint32_t guestMemoryTypeBits = 0;

    const uint32_t hostMemoryTypeBits = memoryRequirements->memoryTypeBits;
    for (uint32_t hostMemoryTypeIndex = 0;
         hostMemoryTypeIndex < mHostMemoryProperties.memoryTypeCount; hostMemoryTypeIndex++) {
        if (!(hostMemoryTypeBits & (1u << hostMemoryTypeIndex))) {
            continue;
        }

        uint32_t guestMemoryTypeIndex = mHostToGuestMemoryTypeIndexMap[hostMemoryTypeIndex];
        if (guestMemoryTypeIndex == kInvalidMemoryTypeIndex) {
            continue;
        }

        guestMemoryTypeBits |= (1u << guestMemoryTypeIndex);
    }

    memoryRequirements->memoryTypeBits = guestMemoryTypeBits;
}

}  // namespace vk
}  // namespace gfxstream
