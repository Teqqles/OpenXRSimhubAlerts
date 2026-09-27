#pragma once
#include "../hooks.h"        // full OpenXR + Vulkan types (Vk*, XR_NULL_HANDLE, PFN_*)
#include "render_backend.h"
#include "xr_swapchain.h"     // XrOverlaySwapchain
#include <vector>

struct Atlas;   // atlas.h

// Real Vulkan overlay renderer. Mirrors D3D12Backend: draws per-vertex-coloured
// shapes, text and icons, each sampled from the distance field atlas, into an
// OpenXR-owned swapchain image which the endFrame hook then references as a
// head-locked quad composition layer.
// Screen-space simple: no depth, straight-alpha blending. Never throws; a
// failed Init() => the overlay stays disabled (session.cpp -> pass-through).
//
// Like the D3D12 path we own our own command pool + command buffer and submit
// to the app's VkQueue, fence-waiting after submit so the GPU has finished
// writing the image before we release it back to the runtime. We do NOT own the
// VkInstance/VkPhysicalDevice/VkDevice/VkImage (app+runtime own them) nor the
// XrSwapchain (runtime owns it): those are never destroyed here.
class VulkanBackend : public IRenderBackend {
public:
  bool Init(XrSession session, const void* graphicsBinding, XrInstance instance) override;
  XrSwapchain Swapchain() const override { return _sc.handle(); }
  int32_t Width() const override { return _sc.width(); }
  int32_t Height() const override { return _sc.height(); }
  bool Render(const OverlayGeometry& geo) override;
  void Release() override;
  ~VulkanBackend() override { Release(); }

private:
  // Creates the atlas image, view, sampler and descriptor set, then uploads the
  // pixels. Needs the command buffer and fence, so it runs last in Init.
  bool CreateAtlas();
  bool UploadAtlas(const Atlas& atlas);

  // App-owned Vulkan objects (borrowed, never destroyed here).
  VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
  VkDevice         _device         = VK_NULL_HANDLE;
  VkQueue          _queue          = VK_NULL_HANDLE;
  uint32_t         _queueFamily    = 0;

  XrOverlaySwapchain _sc;   // OpenXR swapchain + acquire/wait/release lifecycle
  VkFormat           _format = VK_FORMAT_UNDEFINED;

  // Per-swapchain-image render targets. The VkImage handles are runtime-owned
  // (never destroyed here); the views + framebuffers are ours.
  std::vector<VkImageView>   _views;
  std::vector<VkFramebuffer> _framebuffers;

  VkRenderPass          _renderPass    = VK_NULL_HANDLE;
  VkDescriptorSetLayout _descSetLayout = VK_NULL_HANDLE;   // binding 0: atlas sampler
  VkPipelineLayout      _pipeLayout    = VK_NULL_HANDLE;   // set 0: _descSetLayout
  VkPipeline            _pipeline      = VK_NULL_HANDLE;

  // Distance field atlas, uploaded once in Init and sampled by every draw.
  VkImage          _atlasImage  = VK_NULL_HANDLE;
  VkDeviceMemory   _atlasMemory = VK_NULL_HANDLE;
  VkImageView      _atlasView   = VK_NULL_HANDLE;
  VkSampler        _sampler     = VK_NULL_HANDLE;
  VkDescriptorPool _descPool    = VK_NULL_HANDLE;
  VkDescriptorSet  _descSet     = VK_NULL_HANDLE;   // freed with _descPool

  // Host-visible + host-coherent vertex buffer, persistently mapped so per-frame
  // fills need no allocation.
  VkBuffer       _vbuf       = VK_NULL_HANDLE;
  VkDeviceMemory _vbufMemory = VK_NULL_HANDLE;
  void*          _vbufMapped = nullptr;

  VkCommandPool   _cmdPool = VK_NULL_HANDLE;
  VkCommandBuffer _cmdBuf  = VK_NULL_HANDLE;   // freed with the pool
  VkFence         _fence   = VK_NULL_HANDLE;
};
