#include "graphics/host_gpu/utils.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/vma.h"
#include "graphics/presentation/window.h"

#include <atomic>
#include <memory>
#include <span>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>

namespace Libs::Graphics {

static Common::Mutex              g_scratch_buffer_mutex;
static std::unique_ptr<uint8_t[]> g_scratch_buffer;
static uint64_t                   g_scratch_buffer_capacity = 0;

std::vector<ImageBufferCopy> MakeLayeredImageBufferCopies(uint32_t layers, uint64_t slice_size,
                                                          uint32_t pitch, uint32_t width,
                                                          uint32_t           height,
                                                          VkImageAspectFlags aspect) {
	if (layers == 0 || slice_size == 0 || slice_size > UINT32_MAX / layers || pitch < width ||
	    width == 0 || height == 0) {
		EXIT("invalid layered image-buffer copy layout\n");
	}
	std::vector<ImageBufferCopy> regions(layers);
	for (uint32_t layer = 0; layer < layers; layer++) {
		auto& region     = regions[layer];
		region.offset    = static_cast<uint32_t>(slice_size * layer);
		region.pitch     = pitch;
		region.width     = width;
		region.height    = height;
		region.src_layer = layer;
		region.aspect    = aspect;
	}
	return regions;
}

UtilScratchBuffer::UtilScratchBuffer(uint64_t size) {
	EXIT_IF(size == 0);

	g_scratch_buffer_mutex.Lock();

	if (g_scratch_buffer_capacity < size) {
		g_scratch_buffer          = std::make_unique<uint8_t[]>(size);
		g_scratch_buffer_capacity = size;
	}

	m_data = g_scratch_buffer.get();
}

UtilScratchBuffer::~UtilScratchBuffer() {
	g_scratch_buffer_mutex.Unlock();
}

bool UtilBufferIsTiled(uint64_t vaddr, uint64_t size) {
	if ((size & 0x7u) == 0) {
		if (size == 0) {
			return false;
		}

		const auto* ptr     = reinterpret_cast<const uint64_t*>(vaddr);
		const auto* ptr_end = reinterpret_cast<const uint64_t*>(vaddr + size);
		const auto  element = *ptr;

		for (; ptr < ptr_end; ptr++) {
			if (element != *ptr) {
				return true;
			}
		}
		return false;
	}
	return true;
}

void VulkanDeviceWaitIdle(GraphicContext* ctx) {
	Common::Mutex* locked[GraphicContext::QUEUES_NUM] {};
	int            locked_num = 0;

	for (int id = 0; id < GraphicContext::QUEUES_NUM; id++) {
		auto* mutex = ctx->queues[id].mutex;
		if (mutex == nullptr || ctx->queues[id].vk_queue == nullptr) {
			continue;
		}

		bool already_locked = false;
		for (int i = 0; i < locked_num; i++) {
			if (locked[i] == mutex) {
				already_locked = true;
				break;
			}
		}

		if (!already_locked) {
			mutex->Lock();
			locked[locked_num++] = mutex;
		}
	}

	auto result = vkDeviceWaitIdle(ctx->device);

	for (int i = locked_num - 1; i >= 0; i--) {
		locked[i]->Unlock();
	}

	if (result != VK_SUCCESS) {
		LOGF("vkDeviceWaitIdle failed: %s (%d)\n", string_VkResult(result),
		     static_cast<int>(result));
	}
	EXIT_NOT_IMPLEMENTED(result != VK_SUCCESS);
}

void UtilResetImageViews(VulkanImage* image) {
	for (auto& view: image->image_view) {
		view = nullptr;
	}
}

void UtilCreateImageView(GraphicContext* ctx, VulkanImage* image, int view_index,
                         VkImageViewType view_type, VkImageAspectFlags aspect_mask,
                         VkComponentMapping components, uint32_t base_array_layer,
                         uint32_t base_mip_level, uint32_t layer_count, uint32_t level_count,
                         VkFormat view_format, VkImageUsageFlags view_usage) {
	if (view_index < 0 || view_index >= VulkanImage::VIEW_MAX || image->image == nullptr ||
	    image->image_view[view_index] != nullptr) {
		EXIT("invalid image-view creation target: image=%p index=%d current_view=%d\n",
		     static_cast<const void*>(image), view_index,
		     view_index >= 0 && view_index < VulkanImage::VIEW_MAX &&
		         image->image_view[view_index] != nullptr);
	}

	VkImageViewUsageCreateInfo usage_info {};
	usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
	usage_info.pNext = nullptr;
	usage_info.usage = view_usage;

	VkImageViewCreateInfo create_info {};
	create_info.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	create_info.pNext      = (view_usage != 0 ? &usage_info : nullptr);
	create_info.flags      = 0;
	create_info.image      = image->image;
	create_info.viewType   = view_type;
	create_info.format     = (view_format != VK_FORMAT_UNDEFINED ? view_format : image->format);
	create_info.components = components;
	create_info.subresourceRange.aspectMask     = aspect_mask;
	create_info.subresourceRange.baseArrayLayer = base_array_layer;
	create_info.subresourceRange.baseMipLevel   = base_mip_level;
	create_info.subresourceRange.layerCount     = layer_count;
	create_info.subresourceRange.levelCount     = level_count;

	const auto result =
	    vkCreateImageView(ctx->device, &create_info, nullptr, &image->image_view[view_index]);
	if (result != VK_SUCCESS || image->image_view[view_index] == nullptr) {
		EXIT("failed to create image view: result=%d image_format=%d view_format=%d index=%d\n",
		     static_cast<int>(result), static_cast<int>(image->format),
		     static_cast<int>(create_info.format), view_index);
	}
}

static void SetImageLayout(VkCommandBuffer buffer, VulkanImage* dst_image, uint32_t base_level,
                           uint32_t levels, VkImageAspectFlags aspect_mask,
                           VkImageLayout old_image_layout, VkImageLayout new_image_layout);

template <typename Recorder>
static void ExecuteImmediateUtilCommands(const Recorder& recorder) {
	// Keep synchronous utility submission behind one boundary so adopting a central scheduler does
	// not touch every caller.
	CommandBuffer command(GraphicContext::QUEUE_UTIL);
	command.Begin();
	recorder(&command, command.GetPool()->buffers[command.GetIndex()]);
	command.End();
	command.Execute();
	command.WaitForFence();
}

[[nodiscard]] static bool IsSingleImageAspect(VkImageAspectFlags aspect) {
	return aspect == VK_IMAGE_ASPECT_COLOR_BIT || aspect == VK_IMAGE_ASPECT_DEPTH_BIT ||
	       aspect == VK_IMAGE_ASPECT_STENCIL_BIT;
}

[[nodiscard]] static VkBufferImageCopy
MakeBufferImageCopy(VkDeviceSize buffer_offset, uint32_t buffer_pitch, VkImageAspectFlags aspect,
                    uint32_t mip_level, uint32_t array_layer, VkOffset3D image_offset,
                    VkExtent3D image_extent) {
	VkBufferImageCopy copy {};
	copy.bufferOffset                    = buffer_offset;
	copy.bufferRowLength                 = buffer_pitch != image_extent.width ? buffer_pitch : 0;
	copy.bufferImageHeight               = 0;
	copy.imageSubresource.aspectMask     = aspect;
	copy.imageSubresource.mipLevel       = mip_level;
	copy.imageSubresource.baseArrayLayer = array_layer;
	copy.imageSubresource.layerCount     = 1;
	copy.imageOffset                     = image_offset;
	copy.imageExtent                     = image_extent;
	return copy;
}

static void SetBufferMemoryBarrier(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
                                   VkDeviceSize size, VkAccessFlags src_access,
                                   VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                                   VkPipelineStageFlags dst_stage) {
	VkBufferMemoryBarrier barrier {};
	barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask       = src_access;
	barrier.dstAccessMask       = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = buffer;
	barrier.offset              = offset;
	barrier.size                = size;
	vkCmdPipelineBarrier(command, src_stage, dst_stage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

[[nodiscard]] static VkImageAspectFlags GetDepthStencilAspects(VkFormat format) {
	switch (format) {
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT: return VK_IMAGE_ASPECT_DEPTH_BIT;
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default: EXIT("unsupported depth/stencil image format: %d\n", static_cast<int>(format));
	}
}

[[nodiscard]] static VkImageAspectFlags GetTransferAspects(const VulkanImage& image,
                                                           VkImageAspectFlags copy_aspect) {
	switch (copy_aspect) {
		case VK_IMAGE_ASPECT_COLOR_BIT: return VK_IMAGE_ASPECT_COLOR_BIT;
		case VK_IMAGE_ASPECT_DEPTH_BIT:
		case VK_IMAGE_ASPECT_STENCIL_BIT: {
			const auto aspects = GetDepthStencilAspects(image.format);
			if ((copy_aspect & aspects) == 0) {
				EXIT("image transfer aspect is unavailable: format=%d copy=0x%x available=0x%x\n",
				     static_cast<int>(image.format), copy_aspect, aspects);
			}
			return aspects;
		}
		default: EXIT("unsupported image transfer aspect: 0x%x\n", copy_aspect);
	}
}

static void RecordBufferToImageCopy(CommandBuffer& command, VulkanBuffer& src_buffer,
                                    VulkanImage&                       dst_image,
                                    std::span<const VkBufferImageCopy> regions,
                                    VkImageAspectFlags                 transition_aspects,
                                    VkImageLayout initial_layout, VkImageLayout final_layout) {
	auto* vk_command = command.GetPool()->buffers[command.GetIndex()];
	SetBufferMemoryBarrier(vk_command, src_buffer.buffer, 0, VK_WHOLE_SIZE,
	                       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
	                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	SetImageLayout(vk_command, &dst_image, 0, VK_REMAINING_MIP_LEVELS, transition_aspects,
	               initial_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkCmdCopyBufferToImage(vk_command, src_buffer.buffer, dst_image.image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                       static_cast<uint32_t>(regions.size()), regions.data());
	SetImageLayout(vk_command, &dst_image, 0, VK_REMAINING_MIP_LEVELS, transition_aspects,
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout);
}

[[nodiscard]] static std::span<const VkBufferImageCopy>
ConvertBufferImageCopies(std::span<const BufferImageCopy> regions) {
	static thread_local std::vector<VkBufferImageCopy> vk_regions;
	vk_regions.resize(regions.size());

	for (size_t i = 0; i < regions.size(); i++) {
		const auto& r = regions[i];
		vk_regions[i] = MakeBufferImageCopy(
		    r.offset, r.pitch, r.aspect, r.dst_level, r.dst_layer, {r.dst_x, r.dst_y, r.dst_z},
		    {r.width, (r.copy_height != 0 ? r.copy_height : r.height), 1});
	}
	return vk_regions;
}

[[nodiscard]] static std::span<const VkBufferImageCopy>
ConvertImageBufferCopies(std::span<const ImageBufferCopy> regions) {
	static thread_local std::vector<VkBufferImageCopy> vk_regions;
	vk_regions.resize(regions.size());
	for (size_t i = 0; i < regions.size(); i++) {
		const auto& r = regions[i];
		vk_regions[i] = MakeBufferImageCopy(
		    r.offset, r.pitch, r.aspect, r.src_level, r.src_layer, {r.src_x, r.src_y, r.src_z},
		    {r.width, r.copy_height != 0 ? r.copy_height : r.height, 1});
	}
	return vk_regions;
}

static void RecordImageToBuffer(CommandBuffer& command, VulkanImage& src_image,
                                VulkanBuffer& dst_buffer, std::span<const ImageBufferCopy> regions,
                                VkImageLayout final_layout) {
	auto*              vk_command = command.GetPool()->buffers[command.GetIndex()];
	VkImageAspectFlags aspects    = 0;
	for (const auto& region: regions) {
		aspects |= GetTransferAspects(src_image, region.aspect);
	}
	SetImageLayout(vk_command, &src_image, 0, VK_REMAINING_MIP_LEVELS, aspects, src_image.layout,
	               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	SetBufferMemoryBarrier(vk_command, dst_buffer.buffer, 0, VK_WHOLE_SIZE,
	                       VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	const auto copies = ConvertImageBufferCopies(regions);
	vkCmdCopyImageToBuffer(vk_command, src_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       dst_buffer.buffer, static_cast<uint32_t>(copies.size()), copies.data());
	SetBufferMemoryBarrier(vk_command, dst_buffer.buffer, 0, VK_WHOLE_SIZE,
	                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
	                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	SetImageLayout(vk_command, &src_image, 0, VK_REMAINING_MIP_LEVELS, aspects,
	               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, final_layout);
}

static void RecordImageToBuffer(CommandBuffer& command, VulkanImage& src_image,
                                VulkanBuffer& dst_buffer, uint32_t dst_pitch,
                                VkImageLayout final_layout, VkImageAspectFlags aspect) {
	const ImageBufferCopy region {
	    0, dst_pitch, 0, src_image.extent.width, src_image.extent.height, 0, 0, 0, 0, 0, aspect};
	RecordImageToBuffer(command, src_image, dst_buffer,
	                    std::span<const ImageBufferCopy>(&region, 1), final_layout);
}

class ReusableStagingBuffer {
public:
	ReusableStagingBuffer()  = default;
	~ReusableStagingBuffer() = default;
	PS5SIM_CLASS_NO_COPY(ReusableStagingBuffer);

	void UploadToBuffer(GraphicContext* ctx, VulkanBuffer* dst_buffer, const void* src_data,
	                    uint64_t size, uint64_t dst_offset) {
		RecordUpload<true>(ctx, src_data, size, [&](CommandBuffer*, VkCommandBuffer vk_command) {
			SetBufferMemoryBarrier(vk_command, m_buffer.buffer, 0, size, VK_ACCESS_MEMORY_WRITE_BIT,
			                       VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			                       VK_PIPELINE_STAGE_TRANSFER_BIT);
			SetBufferMemoryBarrier(vk_command, dst_buffer->buffer, dst_offset, size,
			                       VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			                       VK_PIPELINE_STAGE_TRANSFER_BIT);
			const VkBufferCopy copy_region {.srcOffset = 0, .dstOffset = dst_offset, .size = size};
			vkCmdCopyBuffer(vk_command, m_buffer.buffer, dst_buffer->buffer, 1, &copy_region);
			SetBufferMemoryBarrier(
			    vk_command, dst_buffer->buffer, dst_offset, size, VK_ACCESS_TRANSFER_WRITE_BIT,
			    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
			        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_TRANSFER_BIT,
			    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		});
	}

	void UploadToImage(GraphicContext* ctx, VulkanImage* image, const void* src_data, uint64_t size,
	                   uint32_t src_pitch, VkImageAspectFlags copy_aspect,
	                   VkImageLayout initial_layout, VkImageLayout final_layout) {
		const auto transition_aspects = GetTransferAspects(*image, copy_aspect);
		RecordUpload<false>(ctx, src_data, size, [&](CommandBuffer* command, VkCommandBuffer) {
			const auto region = MakeBufferImageCopy(0, src_pitch, copy_aspect, 0, 0, {0, 0, 0},
			                                        {image->extent.width, image->extent.height, 1});
			RecordBufferToImageCopy(*command, m_buffer, *image,
			                        std::span<const VkBufferImageCopy>(&region, 1),
			                        transition_aspects, initial_layout, final_layout);
		});
	}

	void UploadToImage(GraphicContext* ctx, VulkanImage* dst_image, const void* src_data,
	                   uint64_t size, std::span<const BufferImageCopy> regions,
	                   uint64_t dst_layout) {
		RecordUpload<false>(ctx, src_data, size, [&](CommandBuffer* command, VkCommandBuffer) {
			VkImageAspectFlags transition_aspects = 0;
			for (const auto& region: regions) {
				transition_aspects |= GetTransferAspects(*dst_image, region.aspect);
			}
			RecordBufferToImageCopy(*command, m_buffer, *dst_image,
			                        ConvertBufferImageCopies(regions), transition_aspects,
			                        dst_image->layout, static_cast<VkImageLayout>(dst_layout));
		});
	}

	void DownloadFromImage(GraphicContext* ctx, void* dst_data, uint64_t size, uint32_t dst_pitch,
	                       VulkanImage* src_image, uint64_t src_layout, VkImageAspectFlags aspect) {
		Common::LockGuard lock(m_mutex);

		EnsureBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		ExecuteImmediateUtilCommands([&](CommandBuffer* command, VkCommandBuffer) {
			RecordImageToBuffer(*command, *src_image, m_buffer, dst_pitch,
			                    static_cast<VkImageLayout>(src_layout), aspect);
		});

		std::memcpy(dst_data, m_mapped_data, size);
	}

	void DownloadFromImage(GraphicContext* ctx, void* dst_data, uint64_t size,
	                       std::span<const ImageBufferCopy> regions, VulkanImage* src_image,
	                       uint64_t src_layout) {
		Common::LockGuard lock(m_mutex);
		EnsureBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		ExecuteImmediateUtilCommands([&](CommandBuffer* command, VkCommandBuffer) {
			RecordImageToBuffer(*command, *src_image, m_buffer, regions,
			                    static_cast<VkImageLayout>(src_layout));
		});
		std::memcpy(dst_data, m_mapped_data, size);
	}

private:
	template <bool WaitIdle, typename Recorder>
	void RecordUpload(GraphicContext* ctx, const void* src_data, uint64_t size,
	                  const Recorder& recorder) {
		Common::LockGuard lock(m_mutex);
		CopyFromHost(ctx, src_data, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		if constexpr (WaitIdle) {
			VulkanDeviceWaitIdle(ctx);
		}
		ExecuteImmediateUtilCommands(recorder);
	}

	void CopyFromHost(GraphicContext* ctx, const void* src_data, uint64_t size,
	                  VkBufferUsageFlags usage) {
		EnsureBuffer(ctx, size, usage);

		std::memcpy(m_mapped_data, src_data, size);
	}

	void EnsureBuffer(GraphicContext* ctx, uint64_t size, VkBufferUsageFlags usage) {
		if (m_capacity < size || m_buffer.usage != usage) {
			if (m_buffer.buffer != nullptr) {
				VulkanUnmapMemory(ctx, &m_buffer.memory);
				m_mapped_data = nullptr;
				VulkanDeleteBuffer(ctx, &m_buffer);
			}

			m_buffer.usage = usage;
			m_buffer.memory.property =
			    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			VulkanCreateBuffer(ctx, size, &m_buffer);
			m_capacity = size;

			VulkanMapMemory(ctx, &m_buffer.memory, &m_mapped_data);
		}
	}

	Common::Mutex m_mutex;
	VulkanBuffer  m_buffer;
	uint64_t      m_capacity    = 0;
	void*         m_mapped_data = nullptr;
};

// Do not replace with one
static ReusableStagingBuffer g_texture_staging_buffer;
static ReusableStagingBuffer g_vertex_staging_buffer;
static ReusableStagingBuffer g_readback_staging_buffer;

static Common::Mutex g_compressed_image_copy_mutex;
static VulkanBuffer  g_compressed_image_copy_buffer;

bool UtilIsBcFormat(VkFormat format) {
	return format == VK_FORMAT_BC1_RGB_UNORM_BLOCK || format == VK_FORMAT_BC1_RGB_SRGB_BLOCK ||
	       format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format == VK_FORMAT_BC1_RGBA_SRGB_BLOCK ||
	       format == VK_FORMAT_BC2_UNORM_BLOCK || format == VK_FORMAT_BC2_SRGB_BLOCK ||
	       format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK ||
	       format == VK_FORMAT_BC4_UNORM_BLOCK || format == VK_FORMAT_BC4_SNORM_BLOCK ||
	       format == VK_FORMAT_BC5_UNORM_BLOCK || format == VK_FORMAT_BC5_SNORM_BLOCK ||
	       format == VK_FORMAT_BC6H_UFLOAT_BLOCK || format == VK_FORMAT_BC6H_SFLOAT_BLOCK ||
	       format == VK_FORMAT_BC7_UNORM_BLOCK || format == VK_FORMAT_BC7_SRGB_BLOCK;
}

uint32_t UtilGetBcBlockSize(VkFormat format) {
	switch (format) {
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK: return 8;
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK: return 16;
		default: return 0;
	}
}

static uint32_t GetFormatTexelSize(VkFormat format) {
	switch (format) {
		case VK_FORMAT_R32G32_SFLOAT:
		case VK_FORMAT_R32G32_UINT: return 8;
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_R32G32B32A32_UINT: return 16;
		default: return 0;
	}
}

static VulkanBuffer* GetCompressedImageCopyBuffer(GraphicContext* ctx, uint64_t size) {
	EXIT_IF(size == 0);

	constexpr uint64_t buffer_size = 64ull * 1024ull * 1024ull;
	EXIT_NOT_IMPLEMENTED(size > buffer_size);

	Common::LockGuard lock(g_compressed_image_copy_mutex);

	if (g_compressed_image_copy_buffer.buffer == nullptr) {
		g_compressed_image_copy_buffer.usage =
		    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		g_compressed_image_copy_buffer.memory.property = 0;
		VulkanCreateBuffer(ctx, buffer_size, &g_compressed_image_copy_buffer);
	}

	return &g_compressed_image_copy_buffer;
}

static ReusableStagingBuffer* GetStagingBuffer(StagingBufferType type) {
	switch (type) {
		case StagingBufferType::Texture: return &g_texture_staging_buffer;
		case StagingBufferType::Vertex: return &g_vertex_staging_buffer;
		case StagingBufferType::ReadBack: return &g_readback_staging_buffer;
		default: EXIT("unknown staging buffer type\n");
	}

	return nullptr;
}

static void SetImageLayout(VkCommandBuffer buffer, VulkanImage* dst_image, uint32_t base_level,
                           uint32_t levels, VkImageAspectFlags aspect_mask,
                           VkImageLayout old_image_layout, VkImageLayout new_image_layout) {
	if ((old_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	     new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) ||
	    (old_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	     new_image_layout == VK_IMAGE_LAYOUT_GENERAL) ||
	    (old_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	     new_image_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)) {
		static std::atomic<uint32_t> log_count {0};
		if (log_count.fetch_add(1, std::memory_order_relaxed) < 64) {
			LOGF("set_image_layout: image=%p type=%d tracked=%d requested=%d->%d base=%u "
			     "levels=%u\n",
			     reinterpret_cast<void*>(dst_image->image), static_cast<int>(dst_image->type),
			     static_cast<int>(dst_image->layout), static_cast<int>(old_image_layout),
			     static_cast<int>(new_image_layout), base_level, levels);
		}
	}

	if (old_image_layout == new_image_layout) {
		dst_image->layout = new_image_layout;
		return;
	}

	VkImageMemoryBarrier image_memory_barrier {};
	image_memory_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.pNext                           = nullptr;
	image_memory_barrier.srcAccessMask                   = 0;
	image_memory_barrier.dstAccessMask                   = 0;
	image_memory_barrier.oldLayout                       = old_image_layout;
	image_memory_barrier.newLayout                       = new_image_layout;
	image_memory_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.image                           = dst_image->image;
	image_memory_barrier.subresourceRange.aspectMask     = aspect_mask;
	image_memory_barrier.subresourceRange.baseMipLevel   = base_level;
	image_memory_barrier.subresourceRange.levelCount     = levels;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.layerCount     = dst_image->layers;

	switch (old_image_layout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			image_memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			image_memory_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			image_memory_barrier.srcAccessMask =
			    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
			    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			    VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			image_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;
		default: break;
	}

	switch (new_image_layout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			image_memory_barrier.srcAccessMask |=
			    VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
			image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			image_memory_barrier.dstAccessMask =
			    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			image_memory_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		default: break;
	}

	auto src_stages = static_cast<VkPipelineStageFlags>(
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
	    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
	    VK_PIPELINE_STAGE_HOST_BIT);
	auto dest_stages = static_cast<VkPipelineStageFlags>(
	    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
	    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

	vkCmdPipelineBarrier(buffer, src_stages, dest_stages, 0, 0, nullptr, 0, nullptr, 1,
	                     &image_memory_barrier);

	dst_image->layout = new_image_layout;
}

static bool CopyBlockImageToCompressedImage(VkCommandBuffer vk_buffer, const ImageImageCopy& r,
                                            VulkanImage* dst_image) {
	if (r.src_aspect != VK_IMAGE_ASPECT_COLOR_BIT || r.dst_aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
	    !UtilIsBcFormat(dst_image->format)) {
		return false;
	}

	auto src_texel_size = GetFormatTexelSize(r.src_image->format);
	auto dst_block_size = UtilGetBcBlockSize(dst_image->format);
	if (src_texel_size == 0 || src_texel_size != dst_block_size) {
		return false;
	}

	auto block_width  = (r.width + 3u) / 4u;
	auto block_height = (r.height + 3u) / 4u;
	if (block_width > r.src_image->extent.width || block_height > r.src_image->extent.height) {
		return false;
	}

	auto* ctx         = WindowGetGraphicContext();
	auto* copy_buffer = GetCompressedImageCopyBuffer(ctx, static_cast<uint64_t>(block_width) *
	                                                          block_height * dst_block_size);

	const auto src_region =
	    MakeBufferImageCopy(0, 0, VK_IMAGE_ASPECT_COLOR_BIT, r.src_level, r.src_layer,
	                        {r.src_x, r.src_y, 0}, {block_width, block_height, 1});

	auto src_layout = r.src_image->layout;
	SetImageLayout(vk_buffer, r.src_image, r.src_level, 1, VK_IMAGE_ASPECT_COLOR_BIT, src_layout,
	               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	SetBufferMemoryBarrier(vk_buffer, copy_buffer->buffer, 0, VK_WHOLE_SIZE,
	                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	vkCmdCopyImageToBuffer(vk_buffer, r.src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       copy_buffer->buffer, 1, &src_region);
	SetBufferMemoryBarrier(vk_buffer, copy_buffer->buffer, 0, VK_WHOLE_SIZE,
	                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
	                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	const auto dst_region =
	    MakeBufferImageCopy(0, 0, VK_IMAGE_ASPECT_COLOR_BIT, r.dst_level, r.dst_layer,
	                        {r.dst_x, r.dst_y, 0}, {r.width, r.height, 1});

	vkCmdCopyBufferToImage(vk_buffer, copy_buffer->buffer, dst_image->image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &dst_region);

	SetImageLayout(vk_buffer, r.src_image, r.src_level, 1, VK_IMAGE_ASPECT_COLOR_BIT,
	               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_layout);

	return true;
}

void UtilImageToImage(CommandBuffer* buffer, std::span<const ImageImageCopy> regions,
                      VulkanImage* dst_image, uint64_t dst_layout) {
	auto* vk_buffer = buffer->GetPool()->buffers[buffer->GetIndex()];

	bool               same_image_copy        = false;
	VkImageAspectFlags dst_transition_aspects = 0;
	for (const auto& r: regions) {
		dst_transition_aspects |= GetTransferAspects(*dst_image, r.dst_aspect);
		if (r.src_image == dst_image) {
			same_image_copy = true;
			break;
		}
	}

	if (same_image_copy) {
		SetImageLayout(vk_buffer, dst_image, 0, VK_REMAINING_MIP_LEVELS, dst_transition_aspects,
		               dst_image->layout, VK_IMAGE_LAYOUT_GENERAL);

		for (const auto& r: regions) {
			VkImageCopy region;

			region.srcSubresource.aspectMask     = r.src_aspect;
			region.srcSubresource.mipLevel       = r.src_level;
			region.srcSubresource.baseArrayLayer = r.src_layer;
			region.srcSubresource.layerCount     = 1;
			region.srcOffset                     = {r.src_x, r.src_y, r.src_z};
			region.dstSubresource.aspectMask     = r.dst_aspect;
			region.dstSubresource.mipLevel       = r.dst_level;
			region.dstSubresource.baseArrayLayer = r.dst_layer;
			region.dstSubresource.layerCount     = 1;
			region.dstOffset                     = {r.dst_x, r.dst_y, r.dst_z};
			region.extent                        = {r.width, r.height, 1};

			auto src_layout     = VK_IMAGE_LAYOUT_GENERAL;
			auto restore_layout = VK_IMAGE_LAYOUT_GENERAL;
			if (r.src_image != dst_image) {
				restore_layout = r.src_image->layout;
				SetImageLayout(vk_buffer, r.src_image, r.src_level, 1,
				               GetTransferAspects(*r.src_image, r.src_aspect), restore_layout,
				               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
				src_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}

			vkCmdCopyImage(vk_buffer, r.src_image->image, src_layout, dst_image->image,
			               VK_IMAGE_LAYOUT_GENERAL, 1, &region);

			if (r.src_image != dst_image) {
				SetImageLayout(vk_buffer, r.src_image, r.src_level, 1,
				               GetTransferAspects(*r.src_image, r.src_aspect),
				               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, restore_layout);
			}
		}

		SetImageLayout(vk_buffer, dst_image, 0, VK_REMAINING_MIP_LEVELS, dst_transition_aspects,
		               VK_IMAGE_LAYOUT_GENERAL, static_cast<VkImageLayout>(dst_layout));
		return;
	}

	SetImageLayout(vk_buffer, dst_image, 0, VK_REMAINING_MIP_LEVELS, dst_transition_aspects,
	               dst_image->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	for (const auto& r: regions) {
		VkImageCopy region;

		auto src_layout = r.src_image->layout;

		if (r.src_aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
		    r.dst_aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
		    CopyBlockImageToCompressedImage(vk_buffer, r, dst_image)) {
			continue;
		}

		region.srcSubresource.aspectMask     = r.src_aspect;
		region.srcSubresource.mipLevel       = r.src_level;
		region.srcSubresource.baseArrayLayer = r.src_layer;
		region.srcSubresource.layerCount     = 1;
		region.srcOffset                     = {r.src_x, r.src_y, r.src_z};
		region.dstSubresource.aspectMask     = r.dst_aspect;
		region.dstSubresource.mipLevel       = r.dst_level;
		region.dstSubresource.baseArrayLayer = r.dst_layer;
		region.dstSubresource.layerCount     = 1;
		region.dstOffset                     = {r.dst_x, r.dst_y, r.dst_z};
		region.extent                        = {r.width, r.height, 1};

		SetImageLayout(vk_buffer, r.src_image, r.src_level, 1,
		               GetTransferAspects(*r.src_image, r.src_aspect), src_layout,
		               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		vkCmdCopyImage(vk_buffer, r.src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               dst_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		SetImageLayout(vk_buffer, r.src_image, r.src_level, 1,
		               GetTransferAspects(*r.src_image, r.src_aspect),
		               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_layout);
	}

	SetImageLayout(vk_buffer, dst_image, 0, VK_REMAINING_MIP_LEVELS, dst_transition_aspects,
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<VkImageLayout>(dst_layout));
}

void UtilCopyImageWithBuffer(CommandBuffer* buffer, GraphicContext* ctx, VulkanImage* src_image,
                             VkImageAspectFlags src_aspect, VulkanImage* dst_image,
                             VkImageAspectFlags dst_aspect, uint32_t bytes_per_element,
                             uint64_t dst_layout) {
	if (!IsSingleImageAspect(src_aspect) || !IsSingleImageAspect(dst_aspect) ||
	    (bytes_per_element != 2 && bytes_per_element != 4) || src_image->layers != 1 ||
	    dst_image->layers != 1 || src_image->mip_levels != 1 || dst_image->mip_levels != 1 ||
	    src_image->extent.width == 0 || src_image->extent.height == 0 ||
	    src_image->extent.width != dst_image->extent.width ||
	    src_image->extent.height != dst_image->extent.height) {
		EXIT("unsupported image-buffer-image copy, src_aspect=0x%x dst_aspect=0x%x bpe=%u "
		     "src=%ux%u/%u/%u dst=%ux%u/%u/%u\n",
		     src_aspect, dst_aspect, bytes_per_element, src_image->extent.width,
		     src_image->extent.height, src_image->layers, src_image->mip_levels,
		     dst_image->extent.width, dst_image->extent.height, dst_image->layers,
		     dst_image->mip_levels);
	}

	const uint64_t row_bytes = static_cast<uint64_t>(src_image->extent.width) * bytes_per_element;
	constexpr uint64_t MAX_COPY_BUFFER_SIZE = 16ull * 1024ull * 1024ull;
	if (row_bytes == 0 || row_bytes > MAX_COPY_BUFFER_SIZE) {
		EXIT("unsupported image-buffer-image row size: 0x%016" PRIx64 "\n", row_bytes);
	}
	const auto rows_per_chunk = static_cast<uint32_t>(
	    std::min<uint64_t>(src_image->extent.height, MAX_COPY_BUFFER_SIZE / row_bytes));
	const auto copy_buffer_size = row_bytes * rows_per_chunk;

	// Ps5Sim does not yet have a cross-queue scheduler. Drain submitted work once before
	// recording the recreate copy; commands already recorded on this buffer remain ordered by the
	// barriers below. The preservation policy stays isolated here so a future scheduler can replace
	// this synchronization without changing texture-cache ownership logic.
	VulkanDeviceWaitIdle(ctx);
	auto* copy_buffer  = new VulkanBuffer;
	copy_buffer->usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	copy_buffer->memory.property = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	VulkanCreateBuffer(ctx, copy_buffer_size, copy_buffer);
	buffer->DeleteAfterFence(copy_buffer);

	auto*      vk_buffer    = buffer->GetPool()->buffers[buffer->GetIndex()];
	const auto src_layout   = src_image->layout;
	const auto final_layout = static_cast<VkImageLayout>(dst_layout);
	SetImageLayout(vk_buffer, src_image, 0, 1, src_aspect, src_layout,
	               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	SetImageLayout(vk_buffer, dst_image, 0, 1, dst_aspect, dst_image->layout,
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	for (uint32_t row = 0; row < src_image->extent.height; row += rows_per_chunk) {
		const auto rows = std::min(rows_per_chunk, src_image->extent.height - row);
		auto copy = MakeBufferImageCopy(0, 0, src_aspect, 0, 0, {0, static_cast<int32_t>(row), 0},
		                                {src_image->extent.width, rows, 1});
		vkCmdCopyImageToBuffer(vk_buffer, src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                       copy_buffer->buffer, 1, &copy);

		SetBufferMemoryBarrier(vk_buffer, copy_buffer->buffer, 0, copy_buffer_size,
		                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		copy.imageSubresource.aspectMask = dst_aspect;
		vkCmdCopyBufferToImage(vk_buffer, copy_buffer->buffer, dst_image->image,
		                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		if (row + rows < src_image->extent.height) {
			SetBufferMemoryBarrier(vk_buffer, copy_buffer->buffer, 0, copy_buffer_size,
			                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		}
	}

	SetImageLayout(vk_buffer, src_image, 0, 1, src_aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	               src_layout);
	SetImageLayout(vk_buffer, dst_image, 0, 1, dst_aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	               final_layout);
}

void UtilBlitPreparedImage(CommandBuffer* buffer, VulkanImage* src_image,
                           VulkanSwapchain* dst_swapchain) {
	auto* vk_buffer = buffer->GetPool()->buffers[buffer->GetIndex()];
	if (src_image->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		EXIT("invalid prepared presentation image, image=%p vk_image=%p layout=%d\n",
		     static_cast<const void*>(src_image),
		     src_image != nullptr ? static_cast<void*>(src_image->image) : nullptr,
		     src_image != nullptr ? static_cast<int>(src_image->layout) : -1);
	}

	VulkanImage swapchain_image(VulkanImageType::Unknown);

	swapchain_image.image  = dst_swapchain->swapchain_images[dst_swapchain->current_index];
	swapchain_image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

	SetImageLayout(vk_buffer, &swapchain_image, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT,
	               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkImageBlit region {};
	region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.mipLevel       = 0;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.layerCount     = 1;
	region.srcOffsets[0].x               = 0;
	region.srcOffsets[0].y               = 0;
	region.srcOffsets[0].z               = 0;
	region.srcOffsets[1].x               = static_cast<int>(src_image->extent.width);
	region.srcOffsets[1].y               = static_cast<int>(src_image->extent.height);
	region.srcOffsets[1].z               = 1;
	region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.mipLevel       = 0;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.layerCount     = 1;
	region.dstOffsets[0].x               = 0;
	region.dstOffsets[0].y               = 0;
	region.dstOffsets[0].z               = 0;
	region.dstOffsets[1].x               = static_cast<int>(dst_swapchain->swapchain_extent.width);
	region.dstOffsets[1].y               = static_cast<int>(dst_swapchain->swapchain_extent.height);
	region.dstOffsets[1].z               = 1;

	vkCmdBlitImage(vk_buffer, src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	               swapchain_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
	               VK_FILTER_LINEAR);
}

void UtilClearColorImage(CommandBuffer* buffer, VulkanImage* image,
                         const VkClearColorValue& color) {
	auto* vk_buffer = buffer->GetPool()->buffers[buffer->GetIndex()];
	SetImageLayout(vk_buffer, image, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT, image->layout,
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	vkCmdClearColorImage(vk_buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
	                     &range);
	SetImageLayout(vk_buffer, image, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT,
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

void VulkanCreateBuffer(GraphicContext* gctx, uint64_t size, VulkanBuffer* buffer) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(buffer->buffer != nullptr);

	VkBufferCreateInfo buffer_info {};
	buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size        = size;
	buffer_info.usage       = buffer->usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo alloc_info {};
	alloc_info.requiredFlags = buffer->memory.property;

	const auto result =
	    vmaCreateBuffer(gctx->allocator, &buffer_info, &alloc_info, &buffer->buffer,
	                    &buffer->memory.allocation, &buffer->memory.allocation_info);
	if (result != VK_SUCCESS) {
		VulkanLogMemoryBudget(gctx);
	}
	EXIT_NOT_IMPLEMENTED(result != VK_SUCCESS);

	vkGetBufferMemoryRequirements(gctx->device, buffer->buffer, &buffer->memory.requirements);

	buffer->memory.type      = buffer->memory.allocation_info.memoryType;
	buffer->memory.memory    = buffer->memory.allocation_info.deviceMemory;
	buffer->memory.offset    = buffer->memory.allocation_info.offset;
	buffer->memory.unique_id = VulkanNextMemoryUniqueId();
	buffer->buffer_size      = size;

	VulkanTrackAllocation(&buffer->memory);
}

void VulkanDeleteBuffer(GraphicContext* gctx, VulkanBuffer* buffer) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(buffer->memory.allocation == nullptr);

	VulkanUntrackAllocation(&buffer->memory);
	vmaDestroyBuffer(gctx->allocator, buffer->buffer, buffer->memory.allocation);
	buffer->buffer                 = nullptr;
	buffer->memory.memory          = nullptr;
	buffer->memory.allocation      = nullptr;
	buffer->memory.allocation_info = {};
	buffer->memory.offset          = 0;
}

bool VulkanCreateImage(GraphicContext* gctx, const VkImageCreateInfo* image_info,
                       VulkanImage* image, VulkanMemory* memory) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(image->image != nullptr);
	EXIT_IF(memory->allocation != nullptr);

	VmaAllocationCreateInfo alloc_info {};
	alloc_info.requiredFlags = memory->property;

	const auto result = vmaCreateImage(gctx->allocator, image_info, &alloc_info, &image->image,
	                                   &memory->allocation, &memory->allocation_info);

	if (result != VK_SUCCESS) {
		VulkanLogMemoryBudget(gctx);
		return false;
	}

	vkGetImageMemoryRequirements(gctx->device, image->image, &memory->requirements);

	memory->type      = memory->allocation_info.memoryType;
	memory->memory    = memory->allocation_info.deviceMemory;
	memory->offset    = memory->allocation_info.offset;
	memory->unique_id = VulkanNextMemoryUniqueId();

	VulkanTrackAllocation(memory);

	return true;
}

void VulkanDeleteImage(GraphicContext* gctx, VulkanImage* image, VulkanMemory* memory) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(memory->allocation == nullptr);

	VulkanUntrackAllocation(memory);
	vmaDestroyImage(gctx->allocator, image->image, memory->allocation);
	image->image            = nullptr;
	memory->memory          = nullptr;
	memory->allocation      = nullptr;
	memory->allocation_info = {};
	memory->offset          = 0;
}

void UtilFillImage(GraphicContext* ctx, VulkanImage* dst_image, const void* src_data, uint64_t size,
                   uint32_t src_pitch, uint64_t dst_layout) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(size == 0);

	GetStagingBuffer(StagingBufferType::Texture)
	    ->UploadToImage(ctx, dst_image, src_data, size, src_pitch, VK_IMAGE_ASPECT_COLOR_BIT,
	                    VK_IMAGE_LAYOUT_UNDEFINED, static_cast<VkImageLayout>(dst_layout));
}

void UtilFillBuffer(GraphicContext* ctx, void* dst_data, uint64_t size, uint32_t dst_pitch,
                    VulkanImage* src_image, uint64_t src_layout, VkImageAspectFlags aspect) {
	PS5SIM_PROFILER_FUNCTION();

	EXIT_IF(size == 0);

	GetStagingBuffer(StagingBufferType::ReadBack)
	    ->DownloadFromImage(ctx, dst_data, size, dst_pitch, src_image, src_layout, aspect);
}

void UtilFillBuffer(GraphicContext* ctx, void* dst_data, uint64_t size,
                    std::span<const ImageBufferCopy> regions, VulkanImage* src_image,
                    uint64_t src_layout) {
	PS5SIM_PROFILER_FUNCTION();
	EXIT_IF(size == 0 || regions.empty());
	GetStagingBuffer(StagingBufferType::ReadBack)
	    ->DownloadFromImage(ctx, dst_data, size, regions, src_image, src_layout);
}

void UtilSetImageLayoutOptimal(VulkanImage* image) {
	ExecuteImmediateUtilCommands([&](CommandBuffer*, VkCommandBuffer vk_command) {
		SetImageLayout(vk_command, image, 0, VK_REMAINING_MIP_LEVELS, VK_IMAGE_ASPECT_COLOR_BIT,
		               image->layout, VK_IMAGE_LAYOUT_GENERAL);
	});
}

void UtilFillImage(GraphicContext* ctx, VulkanImage* image, const void* src_data, uint64_t size,
                   std::span<const BufferImageCopy> regions, uint64_t dst_layout) {
	EXIT_IF(size == 0 || regions.empty());

	GetStagingBuffer(StagingBufferType::Texture)
	    ->UploadToImage(ctx, image, src_data, size, regions, dst_layout);
}

void UtilFillImage(GraphicContext* ctx, std::span<const ImageImageCopy> regions,
                   VulkanImage* dst_image, uint64_t dst_layout) {
	ExecuteImmediateUtilCommands([&](CommandBuffer* command, VkCommandBuffer) {
		UtilImageToImage(command, regions, dst_image, dst_layout);
	});
}

void UtilFillImage(GraphicContext* ctx, DepthStencilVulkanImage* image, const void* src_data,
                   uint64_t size, uint32_t src_pitch, VkImageAspectFlags aspect) {
	EXIT_IF(size == 0);
	GetStagingBuffer(StagingBufferType::Texture)
	    ->UploadToImage(ctx, image, src_data, size, src_pitch, aspect, image->layout,
	                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void UtilUploadBuffer(GraphicContext* ctx, StagingBufferType type, VulkanBuffer* dst_buffer,
                      uint64_t dst_offset, const void* src_data, uint64_t size) {
	EXIT_IF(size == 0);
	EXIT_IF(dst_offset > dst_buffer->buffer_size || size > dst_buffer->buffer_size - dst_offset);
	GetStagingBuffer(type)->UploadToBuffer(ctx, dst_buffer, src_data, size, dst_offset);
}

void UtilCopyBuffer(VulkanBuffer* src_buffer, VulkanBuffer* dst_buffer, uint64_t size) {
	EXIT_IF(size == 0 || size > src_buffer->buffer_size || size > dst_buffer->buffer_size);

	ExecuteImmediateUtilCommands([&](CommandBuffer*, VkCommandBuffer vk_command) {
		SetBufferMemoryBarrier(vk_command, src_buffer->buffer, 0, size, VK_ACCESS_MEMORY_WRITE_BIT,
		                       VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		                       VK_PIPELINE_STAGE_TRANSFER_BIT);
		SetBufferMemoryBarrier(vk_command, dst_buffer->buffer, 0, size,
		                       VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		                       VK_PIPELINE_STAGE_TRANSFER_BIT);
		const VkBufferCopy copy_region {.srcOffset = 0, .dstOffset = 0, .size = size};
		vkCmdCopyBuffer(vk_command, src_buffer->buffer, dst_buffer->buffer, 1, &copy_region);
		SetBufferMemoryBarrier(vk_command, dst_buffer->buffer, 0, size,
		                       VK_ACCESS_TRANSFER_WRITE_BIT,
		                       VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	});
}

void UtilDownloadBuffer(GraphicContext* ctx, VulkanBuffer* src_buffer, uint64_t src_offset,
                        void* dst_data, uint64_t size) {
	EXIT_IF(size == 0);
	EXIT_IF(src_offset > src_buffer->buffer_size || size > src_buffer->buffer_size - src_offset);
	const auto family = ctx->queues[GraphicContext::QUEUE_UTIL].family;
	EXIT_IF(family == static_cast<uint32_t>(-1));
	for (int i = GraphicContext::QUEUE_COMPUTE_START;
	     i < GraphicContext::QUEUE_COMPUTE_START + GraphicContext::QUEUE_COMPUTE_NUM; i++) {
		EXIT_IF(ctx->queues[i].family != family);
	}
	EXIT_IF(ctx->queues[GraphicContext::QUEUE_GFX].family != family);

	if ((src_buffer->memory.property & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
		void* mapped = nullptr;
		VulkanMapMemory(ctx, &src_buffer->memory, &mapped);
		std::memcpy(dst_data, static_cast<const uint8_t*>(mapped) + src_offset, size);
		VulkanUnmapMemory(ctx, &src_buffer->memory);
		return;
	}

	VulkanBuffer readback {};
	readback.usage           = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	readback.memory.property = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
	                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
	                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	VulkanCreateBuffer(ctx, size, &readback);

	ExecuteImmediateUtilCommands([&](CommandBuffer*, VkCommandBuffer vk_command) {
		SetBufferMemoryBarrier(vk_command, src_buffer->buffer, src_offset, size,
		                       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		const VkBufferCopy copy {.srcOffset = src_offset, .dstOffset = 0, .size = size};
		vkCmdCopyBuffer(vk_command, src_buffer->buffer, readback.buffer, 1, &copy);
		SetBufferMemoryBarrier(vk_command, readback.buffer, 0, size, VK_ACCESS_TRANSFER_WRITE_BIT,
		                       VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                       VK_PIPELINE_STAGE_HOST_BIT);
	});

	void* mapped = nullptr;
	VulkanMapMemory(ctx, &readback.memory, &mapped);
	std::memcpy(dst_data, mapped, size);
	VulkanUnmapMemory(ctx, &readback.memory);
	VulkanDeleteBuffer(ctx, &readback);
}

} // namespace Libs::Graphics
