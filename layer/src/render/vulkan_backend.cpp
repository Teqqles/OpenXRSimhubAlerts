// Vulkan overlay backend: draws shapes, text and icons, all sampled from the
// shared distance field atlas, into an OpenXR swapchain that the endFrame hook
// composites as a head-locked quad layer. Mirrors d3d12_backend.cpp; Vulkan
// specifics (own command pool/buffer, fence-sync, render-pass layout
// transition, atlas upload) are documented inline.
#include "vulkan_backend.h"
#include "../atlas.h"   // OverlayAtlas, kAtlasSize
#include "log.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Overlay resolution (kEyeDim), vertex cap (kMaxVerts), clear colour, and the
// shared eye/format helpers live in render_backend.h. The vertex layout is
// OverlayVertex (this backend memcpies the emitted geometry into its buffer).

// Precompiled SPIR-V, so the build does not depend on glslang. Generated with
// the Vulkan SDK's glslc (1.4.350.0) from the sources below:
//   glslc -O --target-env=vulkan1.0 -mfmt=num overlay.vert -o vert.num
//   glslc -O --target-env=vulkan1.0 -mfmt=num overlay.frag -o frag.num
//
//   // overlay.vert
//   #version 450
//   layout(location=0) in vec2 inPos;
//   layout(location=1) in vec2 inUv;
//   layout(location=2) in vec4 inColor;
//   layout(location=0) out vec2 fragUv;
//   layout(location=1) out vec4 fragColor;
//   void main(){ gl_Position = vec4(inPos, 0.0, 1.0); fragUv = inUv; fragColor = inColor; }
//
//   // overlay.frag
//   #version 450
//   layout(set=0, binding=0) uniform sampler2D atlas;
//   layout(location=0) in vec2 fragUv;
//   layout(location=1) in vec4 fragColor;
//   layout(location=0) out vec4 outColor;
//   void main(){
//     float d = texture(atlas, fragUv).r;
//     float w = max(fwidth(d), 1.0 / 255.0);
//     float cover = clamp((d - 0.5) / (2.0 * w) + 0.5, 0.0, 1.0);
//     outColor = vec4(fragColor.rgb, fragColor.a * cover);
//   }
//
// The fragment shader turns the sampled distance into coverage exactly as the
// D3D pixel shader does. Texture v runs down the image (atlas row 0 is v = 0),
// matching the D3D backends.
static const uint32_t kVertSpv[] = {
  0x07230203, 0x00010000, 0x000d000b, 0x00000023, 0x00000000, 0x00020011,
  0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
  0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000b000f, 0x00000000,
  0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00000012, 0x0000001c,
  0x0000001d, 0x0000001f, 0x00000021, 0x00030047, 0x0000000b, 0x00000002,
  0x00050048, 0x0000000b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
  0x0000000b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000000b,
  0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b, 0x00000003,
  0x0000000b, 0x00000004, 0x00040047, 0x00000012, 0x0000001e, 0x00000000,
  0x00040047, 0x0000001c, 0x0000001e, 0x00000000, 0x00040047, 0x0000001d,
  0x0000001e, 0x00000001, 0x00040047, 0x0000001f, 0x0000001e, 0x00000001,
  0x00040047, 0x00000021, 0x0000001e, 0x00000002, 0x00020013, 0x00000002,
  0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
  0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008,
  0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001,
  0x0004001c, 0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b,
  0x00000007, 0x00000006, 0x0000000a, 0x0000000a, 0x00040020, 0x0000000c,
  0x00000003, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000003,
  0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e,
  0x0000000f, 0x00000000, 0x00040017, 0x00000010, 0x00000006, 0x00000002,
  0x00040020, 0x00000011, 0x00000001, 0x00000010, 0x0004003b, 0x00000011,
  0x00000012, 0x00000001, 0x0004002b, 0x00000006, 0x00000014, 0x00000000,
  0x0004002b, 0x00000006, 0x00000015, 0x3f800000, 0x00040020, 0x00000019,
  0x00000003, 0x00000007, 0x00040020, 0x0000001b, 0x00000003, 0x00000010,
  0x0004003b, 0x0000001b, 0x0000001c, 0x00000003, 0x0004003b, 0x00000011,
  0x0000001d, 0x00000001, 0x0004003b, 0x00000019, 0x0000001f, 0x00000003,
  0x00040020, 0x00000020, 0x00000001, 0x00000007, 0x0004003b, 0x00000020,
  0x00000021, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
  0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000010, 0x00000013,
  0x00000012, 0x00050051, 0x00000006, 0x00000016, 0x00000013, 0x00000000,
  0x00050051, 0x00000006, 0x00000017, 0x00000013, 0x00000001, 0x00070050,
  0x00000007, 0x00000018, 0x00000016, 0x00000017, 0x00000014, 0x00000015,
  0x00050041, 0x00000019, 0x0000001a, 0x0000000d, 0x0000000f, 0x0003003e,
  0x0000001a, 0x00000018, 0x0004003d, 0x00000010, 0x0000001e, 0x0000001d,
  0x0003003e, 0x0000001c, 0x0000001e, 0x0004003d, 0x00000007, 0x00000022,
  0x00000021, 0x0003003e, 0x0000001f, 0x00000022, 0x000100fd, 0x00010038,
};

static const uint32_t kFragSpv[] = {
  0x07230203, 0x00010000, 0x000d000b, 0x00000039, 0x00000000, 0x00020011,
  0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
  0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0008000f, 0x00000004,
  0x00000004, 0x6e69616d, 0x00000000, 0x00000010, 0x00000029, 0x0000002b,
  0x00030010, 0x00000004, 0x00000007, 0x00040047, 0x0000000c, 0x00000021,
  0x00000000, 0x00040047, 0x0000000c, 0x00000022, 0x00000000, 0x00040047,
  0x00000010, 0x0000001e, 0x00000000, 0x00040047, 0x00000029, 0x0000001e,
  0x00000000, 0x00040047, 0x0000002b, 0x0000001e, 0x00000001, 0x00020013,
  0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
  0x00000020, 0x00090019, 0x00000009, 0x00000006, 0x00000001, 0x00000000,
  0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x0000000a,
  0x00000009, 0x00040020, 0x0000000b, 0x00000000, 0x0000000a, 0x0004003b,
  0x0000000b, 0x0000000c, 0x00000000, 0x00040017, 0x0000000e, 0x00000006,
  0x00000002, 0x00040020, 0x0000000f, 0x00000001, 0x0000000e, 0x0004003b,
  0x0000000f, 0x00000010, 0x00000001, 0x00040017, 0x00000012, 0x00000006,
  0x00000004, 0x00040015, 0x00000014, 0x00000020, 0x00000000, 0x0004002b,
  0x00000006, 0x0000001a, 0x3b808081, 0x0004002b, 0x00000006, 0x0000001e,
  0x3f000000, 0x0004002b, 0x00000006, 0x00000020, 0x40000000, 0x0004002b,
  0x00000006, 0x00000025, 0x00000000, 0x0004002b, 0x00000006, 0x00000026,
  0x3f800000, 0x00040020, 0x00000028, 0x00000003, 0x00000012, 0x0004003b,
  0x00000028, 0x00000029, 0x00000003, 0x00040020, 0x0000002a, 0x00000001,
  0x00000012, 0x0004003b, 0x0000002a, 0x0000002b, 0x00000001, 0x0004002b,
  0x00000014, 0x0000002f, 0x00000003, 0x00040020, 0x00000030, 0x00000001,
  0x00000006, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
  0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c,
  0x0004003d, 0x0000000e, 0x00000011, 0x00000010, 0x00050057, 0x00000012,
  0x00000013, 0x0000000d, 0x00000011, 0x00050051, 0x00000006, 0x00000016,
  0x00000013, 0x00000000, 0x000400d1, 0x00000006, 0x00000019, 0x00000016,
  0x0007000c, 0x00000006, 0x0000001b, 0x00000001, 0x00000028, 0x00000019,
  0x0000001a, 0x00050083, 0x00000006, 0x0000001f, 0x00000016, 0x0000001e,
  0x00050085, 0x00000006, 0x00000022, 0x00000020, 0x0000001b, 0x00050088,
  0x00000006, 0x00000023, 0x0000001f, 0x00000022, 0x00050081, 0x00000006,
  0x00000024, 0x00000023, 0x0000001e, 0x0008000c, 0x00000006, 0x00000027,
  0x00000001, 0x0000002b, 0x00000024, 0x00000025, 0x00000026, 0x0004003d,
  0x00000012, 0x0000002d, 0x0000002b, 0x00050041, 0x00000030, 0x00000031,
  0x0000002b, 0x0000002f, 0x0004003d, 0x00000006, 0x00000032, 0x00000031,
  0x00050085, 0x00000006, 0x00000034, 0x00000032, 0x00000027, 0x00050051,
  0x00000006, 0x00000035, 0x0000002d, 0x00000000, 0x00050051, 0x00000006,
  0x00000036, 0x0000002d, 0x00000001, 0x00050051, 0x00000006, 0x00000037,
  0x0000002d, 0x00000002, 0x00070050, 0x00000012, 0x00000038, 0x00000035,
  0x00000036, 0x00000037, 0x00000034, 0x0003003e, 0x00000029, 0x00000038,
  0x000100fd, 0x00010038,
};

// First memory type allowed by typeBits that has every flag in want, or
// UINT32_MAX when there is none.
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                        VkMemoryPropertyFlags want) {
  VkPhysicalDeviceMemoryProperties memProps{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) {
      return i;
    }
  }
  return UINT32_MAX;
}

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

  int64_t chosen = 0;
  if (!_sc.Create(session, instance, kVkFormatR8G8B8A8Unorm, kVkFormatR8G8B8A8Srgb,
                  "vulkan: ", chosen)) {
    return false;
  }
  _format = static_cast<VkFormat>(chosen);

  std::vector<XrSwapchainImageVulkanKHR> images;
  if (!_sc.EnumerateImages(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR, "vulkan: ", images)) {
    return false;
  }
  const uint32_t imgCount = static_cast<uint32_t>(images.size());

  // Render pass: single colour attachment.
  // Layout assumption (analog of the DX12 resource-state assumption): the
  // OpenXR Vulkan runtime manages the swapchain image's layout transitions
  // OUTSIDE our submit. We take initialLayout = UNDEFINED (we CLEAR, so prior
  // contents are irrelevant) and finalLayout = COLOR_ATTACHMENT_OPTIMAL, i.e.
  // we leave the image in the attachment-optimal layout for the runtime to
  // consume. Not yet verified in-headset: if a runtime expects the image back
  // in a different layout (e.g. it does not perform its own transition),
  // finalLayout must change.
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
    fbci.width           = static_cast<uint32_t>(_sc.width());
    fbci.height          = static_cast<uint32_t>(_sc.height());
    fbci.layers          = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(_device, &fbci, nullptr, &fb) != VK_SUCCESS) {
      Log("vulkan: vkCreateFramebuffer failed");
      return false;
    }
    _framebuffers.push_back(fb);
  }

  // Pipeline layout: set 0, binding 0 is the atlas as a combined image sampler
  // read by the fragment shader. Positions are NDC and colour is per-vertex, so
  // nothing else is bound.
  {
    VkDescriptorSetLayoutBinding atlasBinding{};
    atlasBinding.binding         = 0;
    atlasBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    atlasBinding.descriptorCount = 1;
    atlasBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings    = &atlasBinding;
    if (vkCreateDescriptorSetLayout(_device, &dslci, nullptr, &_descSetLayout) != VK_SUCCESS) {
      Log("vulkan: vkCreateDescriptorSetLayout failed");
      return false;
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &_descSetLayout;
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
    bind.stride    = sizeof(OverlayVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;         // vec2 pos
    attrs[0].offset   = offsetof(OverlayVertex, x);
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;         // vec2 atlas uv
    attrs[1].offset   = offsetof(OverlayVertex, u);
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;   // vec4 colour
    attrs[2].offset   = offsetof(OverlayVertex, r);

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 3;
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
    bci.size        = sizeof(OverlayVertex) * kMaxVerts;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device, &bci, nullptr, &_vbuf) != VK_SUCCESS) {
      Log("vulkan: vkCreateBuffer failed");
      return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(_device, _vbuf, &req);

    const uint32_t typeIndex = FindMemoryType(
        _physicalDevice, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

  if (!CreateAtlas()) return false;

  Log("vulkan: overlay backend initialised");
  return true;
}

bool VulkanBackend::CreateAtlas() {
  const Atlas& atlas = OverlayAtlas();
  const size_t atlasBytes = static_cast<size_t>(kAtlasSize) * kAtlasSize;
  if (atlas.pixels.size() != atlasBytes) {
    Log("vulkan: atlas pixel buffer has the wrong size");
    return false;
  }

  // Device local R8 image that the copy writes and the fragment shader samples.
  {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8_UNORM;
    ici.extent        = {static_cast<uint32_t>(kAtlasSize), static_cast<uint32_t>(kAtlasSize), 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(_device, &ici, nullptr, &_atlasImage) != VK_SUCCESS) {
      Log("vulkan: atlas vkCreateImage failed");
      return false;
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(_device, _atlasImage, &req);
    const uint32_t typeIndex = FindMemoryType(_physicalDevice, req.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (typeIndex == UINT32_MAX) {
      Log("vulkan: no device-local memory type for the atlas");
      return false;
    }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(_device, &mai, nullptr, &_atlasMemory) != VK_SUCCESS) {
      Log("vulkan: atlas vkAllocateMemory failed");
      return false;
    }
    if (vkBindImageMemory(_device, _atlasImage, _atlasMemory, 0) != VK_SUCCESS) {
      Log("vulkan: atlas vkBindImageMemory failed");
      return false;
    }
  }

  // View and sampler: one 2D mip, bilinear, clamped so edge texels never wrap.
  {
    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image      = _atlasImage;
    ivci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format     = VK_FORMAT_R8_UNORM;
    ivci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(_device, &ivci, nullptr, &_atlasView) != VK_SUCCESS) {
      Log("vulkan: atlas vkCreateImageView failed");
      return false;
    }

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter               = VK_FILTER_LINEAR;
    sci.minFilter               = VK_FILTER_LINEAR;
    sci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipLodBias              = 0.0f;
    sci.anisotropyEnable        = VK_FALSE;
    sci.maxAnisotropy           = 1.0f;
    sci.compareEnable           = VK_FALSE;
    sci.compareOp               = VK_COMPARE_OP_NEVER;
    sci.minLod                  = 0.0f;
    sci.maxLod                  = 0.0f;
    sci.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sci.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(_device, &sci, nullptr, &_sampler) != VK_SUCCESS) {
      Log("vulkan: atlas vkCreateSampler failed");
      return false;
    }
  }

  // One descriptor set pointing at the atlas; it is freed with its pool.
  {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;
    if (vkCreateDescriptorPool(_device, &dpci, nullptr, &_descPool) != VK_SUCCESS) {
      Log("vulkan: vkCreateDescriptorPool failed");
      return false;
    }

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool     = _descPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &_descSetLayout;
    if (vkAllocateDescriptorSets(_device, &dsai, &_descSet) != VK_SUCCESS) {
      Log("vulkan: vkAllocateDescriptorSets failed");
      _descSet = VK_NULL_HANDLE;
      return false;
    }

    VkDescriptorImageInfo dii{};
    dii.sampler     = _sampler;
    dii.imageView   = _atlasView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstSet          = _descSet;
    wds.dstBinding      = 0;
    wds.dstArrayElement = 0;
    wds.descriptorCount = 1;
    wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo      = &dii;
    vkUpdateDescriptorSets(_device, 1, &wds, 0, nullptr);
  }

  return UploadAtlas(atlas);
}

bool VulkanBackend::UploadAtlas(const Atlas& atlas) {
  // Host visible, coherent staging buffer holding the tightly packed rows.
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  auto freeStaging = [&]() {
    if (staging != VK_NULL_HANDLE) vkDestroyBuffer(_device, staging, nullptr);
    if (stagingMemory != VK_NULL_HANDLE) vkFreeMemory(_device, stagingMemory, nullptr);
  };

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size        = static_cast<VkDeviceSize>(atlas.pixels.size());
  bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(_device, &bci, nullptr, &staging) != VK_SUCCESS) {
    Log("vulkan: atlas staging vkCreateBuffer failed");
    staging = VK_NULL_HANDLE;
    return false;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(_device, staging, &req);
  const uint32_t typeIndex = FindMemoryType(
      _physicalDevice, req.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (typeIndex == UINT32_MAX) {
    Log("vulkan: no host-visible+coherent memory type for the atlas staging buffer");
    freeStaging();
    return false;
  }
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize  = req.size;
  mai.memoryTypeIndex = typeIndex;
  if (vkAllocateMemory(_device, &mai, nullptr, &stagingMemory) != VK_SUCCESS) {
    Log("vulkan: atlas staging vkAllocateMemory failed");
    stagingMemory = VK_NULL_HANDLE;
    freeStaging();
    return false;
  }
  if (vkBindBufferMemory(_device, staging, stagingMemory, 0) != VK_SUCCESS) {
    Log("vulkan: atlas staging vkBindBufferMemory failed");
    freeStaging();
    return false;
  }
  void* mapped = nullptr;
  if (vkMapMemory(_device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS || !mapped) {
    Log("vulkan: atlas staging vkMapMemory failed");
    freeStaging();
    return false;
  }
  std::memcpy(mapped, atlas.pixels.data(), atlas.pixels.size());
  vkUnmapMemory(_device, stagingMemory);

  // Record the upload on our own command buffer, still in its initial state
  // here. Render() resets it before every use, so leaving it executed is fine.
  VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(_cmdBuf, &cbbi) != VK_SUCCESS) {
    Log("vulkan: atlas upload vkBeginCommandBuffer failed");
    freeStaging();
    return false;
  }

  VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  toDst.srcAccessMask       = 0;
  toDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
  toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
  toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.image               = _atlasImage;
  toDst.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(_cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &toDst);

  // Row length and image height 0: rows are tightly packed and row 0 is the
  // top of the image, as in Atlas::pixels.
  VkBufferImageCopy region{};
  region.bufferOffset      = 0;
  region.bufferRowLength   = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset       = {0, 0, 0};
  region.imageExtent       = {static_cast<uint32_t>(kAtlasSize), static_cast<uint32_t>(kAtlasSize), 1};
  vkCmdCopyBufferToImage(_cmdBuf, staging, _atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);

  VkImageMemoryBarrier toRead = toDst;
  toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(_cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &toRead);

  if (vkEndCommandBuffer(_cmdBuf) != VK_SUCCESS) {
    Log("vulkan: atlas upload vkEndCommandBuffer failed");
    freeStaging();
    return false;
  }

  // A failed submit leaves the resources it references untouched, so the
  // staging buffer can go straight away.
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers    = &_cmdBuf;
  if (vkQueueSubmit(_queue, 1, &si, _fence) != VK_SUCCESS) {
    Log("vulkan: atlas upload vkQueueSubmit failed");
    freeStaging();
    return false;
  }

  // The copy reads the staging buffer, so it may be freed only once the fence
  // says the GPU is done. If the wait fails we cannot know that, so the buffer
  // and its memory are deliberately leaked rather than freed under the GPU.
  if (vkWaitForFences(_device, 1, &_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    Log("vulkan: atlas upload fence wait failed; leaking the staging buffer");
    return false;
  }
  freeStaging();

  // Render() submits with _fence and expects it unsignaled.
  if (vkResetFences(_device, 1, &_fence) != VK_SUCCESS) {
    Log("vulkan: atlas upload vkResetFences failed");
    return false;
  }
  return true;
}

bool VulkanBackend::Render(const OverlayGeometry& geo) {
  if (_sc.handle() == XR_NULL_HANDLE || _framebuffers.empty() ||
      _cmdBuf == VK_NULL_HANDLE || _queue == VK_NULL_HANDLE || _fence == VK_NULL_HANDLE ||
      _descSet == VK_NULL_HANDLE) {
    return false;
  }

  uint32_t index = 0;
  if (!_sc.AcquireWait(index)) return false;

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
        uint32_t nL = 0, nR = 0;
        ClampEyeCounts(geo, nL, nR);
        OverlayVertex* v = static_cast<OverlayVertex*>(_vbufMapped);
        if (nL) std::memcpy(v,      geo.leftEye.data(),  nL * sizeof(OverlayVertex));
        if (nR) std::memcpy(v + nL, geo.rightEye.data(), nR * sizeof(OverlayVertex));

        VkClearValue clear{};
        clear.color = {{kOverlayClearColor[0], kOverlayClearColor[1],
                        kOverlayClearColor[2], kOverlayClearColor[3]}};

        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass        = _renderPass;
        rpbi.framebuffer       = _framebuffers[index];
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = {static_cast<uint32_t>(_sc.width()), static_cast<uint32_t>(_sc.height())};
        rpbi.clearValueCount   = 1;
        rpbi.pClearValues      = &clear;
        vkCmdBeginRenderPass(_cmdBuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdBindDescriptorSets(_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeLayout,
                                0, 1, &_descSet, 0, nullptr);
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

  const bool released = _sc.Release();

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
  if (_descPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(_device, _descPool, nullptr);   // frees _descSet
    _descPool = VK_NULL_HANDLE;
    _descSet  = VK_NULL_HANDLE;
  }
  if (_descSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(_device, _descSetLayout, nullptr); _descSetLayout = VK_NULL_HANDLE; }
  if (_sampler != VK_NULL_HANDLE) { vkDestroySampler(_device, _sampler, nullptr); _sampler = VK_NULL_HANDLE; }
  if (_atlasView != VK_NULL_HANDLE) { vkDestroyImageView(_device, _atlasView, nullptr); _atlasView = VK_NULL_HANDLE; }
  if (_atlasImage != VK_NULL_HANDLE) { vkDestroyImage(_device, _atlasImage, nullptr); _atlasImage = VK_NULL_HANDLE; }
  if (_atlasMemory != VK_NULL_HANDLE) { vkFreeMemory(_device, _atlasMemory, nullptr); _atlasMemory = VK_NULL_HANDLE; }
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
  _sc.forget();
  _queue          = VK_NULL_HANDLE;
  _device         = VK_NULL_HANDLE;
  _physicalDevice = VK_NULL_HANDLE;
}

IRenderBackend* CreateVulkanBackend() { return new VulkanBackend(); }
