// Vulkan overlay backend: draws flat coloured quads into an OpenXR swapchain
// that the endFrame hook composites as a head-locked quad layer. Mirrors
// d3d12_backend.cpp; Vulkan specifics (own command pool/buffer, fence-sync,
// render-pass layout transition) are documented inline.
#include "vulkan_backend.h"
#include "log.h"
#include "shm_contract.h"   // MAX_CARS

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Overlay resolution. The texture is split into two eye halves side by side:
// kEyeDim x kEyeDim each (left eye left half, right eye right half).
constexpr int32_t  kEyeDim   = 512;
// Generous cap: both eye lists concatenated. See d3d11_backend.cpp.
constexpr uint32_t kMaxVerts = 4096;

// Layout matches OverlayVertex exactly, so we memcpy the emitted geometry.
struct Vertex {
  float x, y;        // NDC position (location 0, vec2)
  float r, g, b, a;  // straight-alpha colour (location 1, vec4)
};

// Precompiled SPIR-V. Generated during development with the Vulkan SDK's
// glslc (1.4.350.0): the build does NOT depend on glslang. See the .cpp header
// comment / task report for the exact GLSL sources and compile command.
//
// Vertex shader: passthrough vec2 NDC position + per-vertex vec4 colour.
//   #version 450
//   layout(location=0) in vec2 inPos;  layout(location=1) in vec4 inColor;
//   layout(location=0) out vec4 fragColor;
//   void main(){ gl_Position = vec4(inPos,0.0,1.0); fragColor = inColor; }
static const uint32_t kVertSpv[] = {
  0x07230203, 0x00010000, 0x000d000b, 0x0000001f, 0x00000000, 0x00020011,
  0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
  0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0009000f, 0x00000000,
  0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00000012, 0x0000001b,
  0x0000001d, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b,
  0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000000b, 0x00000001,
  0x0000000b, 0x00000001, 0x00050048, 0x0000000b, 0x00000002, 0x0000000b,
  0x00000003, 0x00050048, 0x0000000b, 0x00000003, 0x0000000b, 0x00000004,
  0x00040047, 0x00000012, 0x0000001e, 0x00000000, 0x00040047, 0x0000001b,
  0x0000001e, 0x00000000, 0x00040047, 0x0000001d, 0x0000001e, 0x00000001,
  0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
  0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
  0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008,
  0x00000009, 0x00000001, 0x0004001c, 0x0000000a, 0x00000006, 0x00000009,
  0x0006001e, 0x0000000b, 0x00000007, 0x00000006, 0x0000000a, 0x0000000a,
  0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c,
  0x0000000d, 0x00000003, 0x00040015, 0x0000000e, 0x00000020, 0x00000001,
  0x0004002b, 0x0000000e, 0x0000000f, 0x00000000, 0x00040017, 0x00000010,
  0x00000006, 0x00000002, 0x00040020, 0x00000011, 0x00000001, 0x00000010,
  0x0004003b, 0x00000011, 0x00000012, 0x00000001, 0x0004002b, 0x00000006,
  0x00000014, 0x00000000, 0x0004002b, 0x00000006, 0x00000015, 0x3f800000,
  0x00040020, 0x00000019, 0x00000003, 0x00000007, 0x0004003b, 0x00000019,
  0x0000001b, 0x00000003, 0x00040020, 0x0000001c, 0x00000001, 0x00000007,
  0x0004003b, 0x0000001c, 0x0000001d, 0x00000001, 0x00050036, 0x00000002,
  0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
  0x00000010, 0x00000013, 0x00000012, 0x00050051, 0x00000006, 0x00000016,
  0x00000013, 0x00000000, 0x00050051, 0x00000006, 0x00000017, 0x00000013,
  0x00000001, 0x00070050, 0x00000007, 0x00000018, 0x00000016, 0x00000017,
  0x00000014, 0x00000015, 0x00050041, 0x00000019, 0x0000001a, 0x0000000d,
  0x0000000f, 0x0003003e, 0x0000001a, 0x00000018, 0x0004003d, 0x00000007,
  0x0000001e, 0x0000001d, 0x0003003e, 0x0000001b, 0x0000001e, 0x000100fd,
  0x00010038,
};

// Fragment shader: outputs the interpolated per-vertex colour.
//   #version 450
//   layout(location=0) in vec4 fragColor;  layout(location=0) out vec4 outColor;
//   void main(){ outColor = fragColor; }
static const uint32_t kFragSpv[] = {
  0x07230203, 0x00010000, 0x000d000b, 0x0000000d, 0x00000000, 0x00020011,
  0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
  0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
  0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030010,
  0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000,
  0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00020013, 0x00000002,
  0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
  0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
  0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003,
  0x00040020, 0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a,
  0x0000000b, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
  0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
  0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x000100fd, 0x00010038,
};

// Vulkan VkFormat enum values (avoid depending on their symbolic names being in
// scope for the OpenXR int64 format list).
constexpr int64_t kVkFormatR8G8B8A8Unorm = 37;   // VK_FORMAT_R8G8B8A8_UNORM
constexpr int64_t kVkFormatR8G8B8A8Srgb  = 43;   // VK_FORMAT_R8G8B8A8_SRGB

}  // namespace

bool VulkanBackend::Init(XrSession session, const void* graphicsBinding, XrInstance instance) {
  const auto* binding = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(graphicsBinding);
  if (!binding || binding->device == VK_NULL_HANDLE || binding->physicalDevice == VK_NULL_HANDLE) {
    Log("vulkan: no device/physicalDevice in binding");
    return false;
  }

  _device         = binding->device;
  _physicalDevice = binding->physicalDevice;
  _queueFamily    = binding->queueFamilyIndex;
  vkGetDeviceQueue(_device, binding->queueFamilyIndex, binding->queueIndex, &_queue);
  if (_queue == VK_NULL_HANDLE) { Log("vulkan: vkGetDeviceQueue returned null"); return false; }

  PFN_xrGetInstanceProcAddr gipa = g_dispatch.getInstanceProcAddr;
  if (!gipa) { Log("vulkan: no getInstanceProcAddr"); return false; }

  PFN_xrCreateSwapchain           pfnCreate = nullptr;
  PFN_xrEnumerateSwapchainImages  pfnEnumImg = nullptr;
  PFN_xrEnumerateSwapchainFormats pfnEnumFmt = nullptr;
  gipa(instance, "xrCreateSwapchain",           reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreate));
  gipa(instance, "xrEnumerateSwapchainImages",  reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumImg));
  gipa(instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumFmt));
  gipa(instance, "xrAcquireSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_acquire));
  gipa(instance, "xrWaitSwapchainImage",        reinterpret_cast<PFN_xrVoidFunction*>(&_wait));
  gipa(instance, "xrReleaseSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_release));
  if (!pfnCreate || !pfnEnumImg || !pfnEnumFmt || !_acquire || !_wait || !_release) {
    Log("vulkan: swapchain entry points unresolved");
    return false;
  }

  // Pick a runtime-supported colour format (Vulkan VkFormat values as int64),
  // preferring plain RGBA8 UNORM (37), then its sRGB sibling (43), else first.
  uint32_t fmtCount = 0;
  if (XR_FAILED(pfnEnumFmt(session, 0, &fmtCount, nullptr)) || fmtCount == 0) {
    Log("vulkan: no swapchain formats");
    return false;
  }
  std::vector<int64_t> formats(fmtCount);
  if (XR_FAILED(pfnEnumFmt(session, fmtCount, &fmtCount, formats.data()))) {
    Log("vulkan: enumerate formats failed");
    return false;
  }
  int64_t chosen = -1;
  for (int64_t f : formats) if (f == kVkFormatR8G8B8A8Unorm) { chosen = f; break; }
  if (chosen < 0) for (int64_t f : formats) if (f == kVkFormatR8G8B8A8Srgb) { chosen = f; break; }
  if (chosen < 0) chosen = formats[0];
  _format = static_cast<VkFormat>(chosen);

  XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  sci.format      = chosen;
  sci.sampleCount = 1;
  sci.width       = kEyeDim * 2;
  sci.height      = kEyeDim;
  sci.faceCount   = 1;
  sci.arraySize   = 1;
  sci.mipCount    = 1;
  if (XR_FAILED(pfnCreate(session, &sci, &_swapchain)) || _swapchain == XR_NULL_HANDLE) {
    Log("vulkan: xrCreateSwapchain failed");
    return false;
  }
  _width  = kEyeDim * 2;
  _height = kEyeDim;

  uint32_t imgCount = 0;
  if (XR_FAILED(pfnEnumImg(_swapchain, 0, &imgCount, nullptr)) || imgCount == 0) {
    Log("vulkan: no swapchain images");
    return false;
  }
  std::vector<XrSwapchainImageVulkanKHR> images(
      imgCount, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
  if (XR_FAILED(pfnEnumImg(_swapchain, imgCount, &imgCount,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
    Log("vulkan: enumerate images failed");
    return false;
  }

  // Render pass: single colour attachment.
  // Layout assumption (analog of the DX12 resource-state assumption): the
  // OpenXR Vulkan runtime manages the swapchain image's layout transitions
  // OUTSIDE our submit. We take initialLayout = UNDEFINED (we CLEAR, so prior
  // contents are irrelevant) and finalLayout = COLOR_ATTACHMENT_OPTIMAL, i.e.
  // we leave the image in the attachment-optimal layout for the runtime to
  // consume. FLAGGED for in-headset verification in Task 13 -- if a given
  // runtime instead expects the image handed back in a specific layout (e.g.
  // it does not perform its own transition), finalLayout must change.
  {
    VkAttachmentDescription color{};
    color.format         = _format;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // External->subpass dependency so the colour-attachment writes are ordered
    // after the runtime's ownership handoff.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &color;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;
    if (vkCreateRenderPass(_device, &rpci, nullptr, &_renderPass) != VK_SUCCESS) {
      Log("vulkan: vkCreateRenderPass failed");
      return false;
    }
  }

  // Image views + framebuffers, one per swapchain image.
  _views.reserve(imgCount);
  _framebuffers.reserve(imgCount);
  for (uint32_t i = 0; i < imgCount; ++i) {
    if (images[i].image == VK_NULL_HANDLE) { Log("vulkan: null swapchain image"); return false; }

    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image        = images[i].image;
    ivci.viewType     = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format       = _format;
    ivci.components   = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel   = 0;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount     = 1;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(_device, &ivci, nullptr, &view) != VK_SUCCESS) {
      Log("vulkan: vkCreateImageView failed");
      return false;
    }
    _views.push_back(view);

    VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass      = _renderPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments    = &view;
    fbci.width           = static_cast<uint32_t>(_width);
    fbci.height          = static_cast<uint32_t>(_height);
    fbci.layers          = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(_device, &fbci, nullptr, &fb) != VK_SUCCESS) {
      Log("vulkan: vkCreateFramebuffer failed");
      return false;
    }
    _framebuffers.push_back(fb);
  }

  // Empty pipeline layout: no descriptors / push constants (positions are NDC,
  // colour is per-vertex).
  {
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (vkCreatePipelineLayout(_device, &plci, nullptr, &_pipeLayout) != VK_SUCCESS) {
      Log("vulkan: vkCreatePipelineLayout failed");
      return false;
    }
  }

  // Graphics pipeline from the embedded SPIR-V.
  {
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(kVertSpv);
    smci.pCode    = kVertSpv;
    if (vkCreateShaderModule(_device, &smci, nullptr, &vs) != VK_SUCCESS) {
      Log("vulkan: vertex shader module failed");
      return false;
    }
    smci.codeSize = sizeof(kFragSpv);
    smci.pCode    = kFragSpv;
    if (vkCreateShaderModule(_device, &smci, nullptr, &fs) != VK_SUCCESS) {
      Log("vulkan: fragment shader module failed");
      vkDestroyShaderModule(_device, vs, nullptr);
      return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;         // vec2 pos
    attrs[0].offset   = offsetof(Vertex, x);
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;   // vec4 colour
    attrs[1].offset   = offsetof(Vertex, r);

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport + scissor are DYNAMIC: Render() sets them per eye (left half then
    // right half) so the two eye lists draw into their own halves of the target.
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    const VkDynamicState kDynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = kDynStates;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight-alpha "over" blend, matching the D3D backends.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vpState;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = nullptr;   // no depth/stencil
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = _pipeLayout;
    gpci.renderPass          = _renderPass;
    gpci.subpass             = 0;

    VkResult pr = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &_pipeline);
    vkDestroyShaderModule(_device, vs, nullptr);
    vkDestroyShaderModule(_device, fs, nullptr);
    if (pr != VK_SUCCESS || _pipeline == VK_NULL_HANDLE) {
      Log("vulkan: vkCreateGraphicsPipelines failed");
      return false;
    }
  }

  // Host-visible + host-coherent vertex buffer, persistently mapped.
  {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = sizeof(Vertex) * kMaxVerts;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device, &bci, nullptr, &_vbuf) != VK_SUCCESS) {
      Log("vulkan: vkCreateBuffer failed");
      return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(_device, _vbuf, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProps);
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
      if ((req.memoryTypeBits & (1u << i)) &&
          (memProps.memoryTypes[i].propertyFlags & want) == want) {
        typeIndex = i;
        break;
      }
    }
    if (typeIndex == UINT32_MAX) {
      Log("vulkan: no host-visible+coherent memory type");
      return false;
    }

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(_device, &mai, nullptr, &_vbufMemory) != VK_SUCCESS) {
      Log("vulkan: vkAllocateMemory failed");
      return false;
    }
    if (vkBindBufferMemory(_device, _vbuf, _vbufMemory, 0) != VK_SUCCESS) {
      Log("vulkan: vkBindBufferMemory failed");
      return false;
    }
    if (vkMapMemory(_device, _vbufMemory, 0, VK_WHOLE_SIZE, 0, &_vbufMapped) != VK_SUCCESS ||
        !_vbufMapped) {
      Log("vulkan: vkMapMemory failed");
      return false;
    }
  }

  // Command pool + one primary command buffer (reset per frame).
  {
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = _queueFamily;
    if (vkCreateCommandPool(_device, &cpci, nullptr, &_cmdPool) != VK_SUCCESS) {
      Log("vulkan: vkCreateCommandPool failed");
      return false;
    }
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = _cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(_device, &cbai, &_cmdBuf) != VK_SUCCESS) {
      Log("vulkan: vkAllocateCommandBuffers failed");
      return false;
    }
  }

  // Fence for CPU-GPU sync (created unsignaled; reset after each wait).
  {
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(_device, &fci, nullptr, &_fence) != VK_SUCCESS) {
      Log("vulkan: vkCreateFence failed");
      return false;
    }
  }

  Log("vulkan: overlay backend initialised");
  return true;
}

bool VulkanBackend::Render(const OverlayGeometry& geo) {
  if (_swapchain == XR_NULL_HANDLE || _framebuffers.empty() ||
      _cmdBuf == VK_NULL_HANDLE || _queue == VK_NULL_HANDLE || _fence == VK_NULL_HANDLE) {
    return false;
  }

  uint32_t index = 0;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(_acquire(_swapchain, &ai, &index))) return false;

  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(_wait(_swapchain, &wi))) {
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    _release(_swapchain, &ri);   // release what we acquired
    return false;
  }

  bool drew = false;

  if (index < _framebuffers.size()) {
    // We own the command buffer, so each frame starts from cleared state.
    if (vkResetCommandBuffer(_cmdBuf, 0) == VK_SUCCESS) {
      VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      if (vkBeginCommandBuffer(_cmdBuf, &bi) == VK_SUCCESS) {
        // Concatenate both eye triangle lists into the persistently mapped
        // buffer (no per-frame heap alloc); each is drawn into its own half of
        // the target below via a dynamic viewport+scissor.
        uint32_t nL = static_cast<uint32_t>(geo.leftEye.size());
        uint32_t nR = static_cast<uint32_t>(geo.rightEye.size());
        if (nL > kMaxVerts) nL = kMaxVerts;
        if (nL + nR > kMaxVerts) nR = kMaxVerts - nL;
        Vertex* v = static_cast<Vertex*>(_vbufMapped);
        if (nL) std::memcpy(v,      geo.leftEye.data(),  nL * sizeof(Vertex));
        if (nR) std::memcpy(v + nL, geo.rightEye.data(), nR * sizeof(Vertex));

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};   // transparent

        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass        = _renderPass;
        rpbi.framebuffer       = _framebuffers[index];
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = {static_cast<uint32_t>(_width), static_cast<uint32_t>(_height)};
        rpbi.clearValueCount   = 1;
        rpbi.pClearValues      = &clear;
        vkCmdBeginRenderPass(_cmdBuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(_cmdBuf, 0, 1, &_vbuf, &offset);

        // Left eye -> left half, right eye -> right half (dynamic viewport).
        VkViewport vp{};
        vp.y = 0.0f;
        vp.width  = static_cast<float>(kEyeDim);
        vp.height = static_cast<float>(kEyeDim);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        VkRect2D sc{};
        sc.extent = {static_cast<uint32_t>(kEyeDim), static_cast<uint32_t>(kEyeDim)};

        vp.x = 0.0f; sc.offset = {0, 0};
        vkCmdSetViewport(_cmdBuf, 0, 1, &vp);
        vkCmdSetScissor(_cmdBuf, 0, 1, &sc);
        if (nL) vkCmdDraw(_cmdBuf, nL, 1, 0, 0);

        vp.x = static_cast<float>(kEyeDim); sc.offset = {kEyeDim, 0};
        vkCmdSetViewport(_cmdBuf, 0, 1, &vp);
        vkCmdSetScissor(_cmdBuf, 0, 1, &sc);
        if (nR) vkCmdDraw(_cmdBuf, nR, 1, nL, 0);

        vkCmdEndRenderPass(_cmdBuf);

        if (vkEndCommandBuffer(_cmdBuf) == VK_SUCCESS) {
          VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
          si.commandBufferCount = 1;
          si.pCommandBuffers    = &_cmdBuf;
          if (vkQueueSubmit(_queue, 1, &si, _fence) == VK_SUCCESS) {
            // Block until the GPU has finished writing the image, so it is ready
            // before we release it back to the runtime.
            vkWaitForFences(_device, 1, &_fence, VK_TRUE, UINT64_MAX);
            vkResetFences(_device, 1, &_fence);
            drew = true;
          }
        }
      }
    }
  }

  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  const bool released = XR_SUCCEEDED(_release(_swapchain, &ri));

  // True only when acquired + waited + drawn + released all succeeded.
  return drew && released;
}

void VulkanBackend::Release() {
  if (_device != VK_NULL_HANDLE) vkDeviceWaitIdle(_device);

  if (_fence != VK_NULL_HANDLE) { vkDestroyFence(_device, _fence, nullptr); _fence = VK_NULL_HANDLE; }
  if (_cmdPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(_device, _cmdPool, nullptr);   // frees _cmdBuf
    _cmdPool = VK_NULL_HANDLE;
    _cmdBuf  = VK_NULL_HANDLE;
  }
  if (_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(_device, _pipeline, nullptr); _pipeline = VK_NULL_HANDLE; }
  if (_pipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(_device, _pipeLayout, nullptr); _pipeLayout = VK_NULL_HANDLE; }
  for (VkFramebuffer fb : _framebuffers) if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(_device, fb, nullptr);
  _framebuffers.clear();
  for (VkImageView v : _views) if (v != VK_NULL_HANDLE) vkDestroyImageView(_device, v, nullptr);
  _views.clear();
  if (_renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(_device, _renderPass, nullptr); _renderPass = VK_NULL_HANDLE; }
  if (_vbufMemory != VK_NULL_HANDLE && _vbufMapped) { vkUnmapMemory(_device, _vbufMemory); _vbufMapped = nullptr; }
  if (_vbuf != VK_NULL_HANDLE) { vkDestroyBuffer(_device, _vbuf, nullptr); _vbuf = VK_NULL_HANDLE; }
  if (_vbufMemory != VK_NULL_HANDLE) { vkFreeMemory(_device, _vbufMemory, nullptr); _vbufMemory = VK_NULL_HANDLE; }

  // Note: the XrSwapchain is owned by the runtime (destroyed with the session);
  // the VkImages come from it; and the VkDevice/VkInstance/VkPhysicalDevice are
  // app-owned. We deliberately destroy none of those here.
  _swapchain = XR_NULL_HANDLE;
  _queue          = VK_NULL_HANDLE;
  _device         = VK_NULL_HANDLE;
  _physicalDevice = VK_NULL_HANDLE;
}

IRenderBackend* CreateVulkanBackend() { return new VulkanBackend(); }
