#include "graphics/host_gpu/renderer/tiler.h"

#include "common/assert.h"
#include "graphics/guest_gpu/gpu_defs.h"
#include "graphics/guest_gpu/tile.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/objects/textureCommon.h"
#include "graphics/host_gpu/renderer/image.h"
#include "graphics/host_gpu/utils.h"

#include <vector>

namespace Libs::Graphics {

void Tiler::DetileImage(GraphicContext* ctx, GpuTextureVulkanImage* image, const ImageInfo& info,
                        const BufferImageCopySource& source, bool refresh, bool storage) const {
	if (ctx == nullptr || image == nullptr || info.address == 0 || info.size == 0 ||
	    info.width == 0 || info.height == 0 || info.depth == 0 || info.levels == 0 ||
	    info.levels >= 16 || !source.cpu_current || source.address != info.address ||
	    source.size != info.size || (source.buffer == nullptr && source.offset != 0)) {
		EXIT("Tiler: unsupported sampled-image detile, ctx=%p image=%p source_buffer=%p "
		     "source=0x%016" PRIx64 "+0x%016" PRIx64 " offset=0x%016" PRIx64
		     " current=%d image=0x%016" PRIx64 "+0x%016" PRIx64
		     " extent=%ux%ux%u levels=%u tile=%u storage=%d\n",
		     static_cast<const void*>(ctx), static_cast<const void*>(image),
		     static_cast<const void*>(source.buffer), source.address, source.size, source.offset,
		     source.cpu_current, info.address, info.size, info.width, info.height, info.depth,
		     info.levels, info.tile, storage);
	}
	if (refresh) {
		VulkanDeviceWaitIdle(ctx);
	}
	const bool array_texture  = TextureIsLayeredTexture(info.type);
	const bool volume_texture = TextureIs3DTexture(info.type);
	auto       layout         = TextureCalcUploadLayout(
	    info.format, info.width, info.height, info.levels, info.depth, info.pitch, info.tile,
	    info.size, true, false, volume_texture, storage ? "StorageTextureCache" : "TextureCache");
	const auto slice_layout = TextureUploadSliceLayout::MipChainPerSlice;
	auto regions = TextureBuildUploadRegions(layout, image->format, info.width, info.height,
	                                         info.depth, info.levels, array_texture, volume_texture,
	                                         TextureUploadDestination::MipLevels, slice_layout);
	// Keep the buffer owner explicit at the detiling seam. The current PS5
	// backend consumes the coherent guest publication; a GPU detiler can consume source.buffer and
	// source.offset here without changing TextureCache ownership or alias classification.
	TextureUploadGuestImage(
	    ctx, image, reinterpret_cast<const void*>(source.address), info.size, regions, layout,
	    info.format, info.width, info.height, info.depth, info.levels, slice_layout,
	    storage ? "StorageTextureCache" : "TextureCache",
	    static_cast<uint64_t>(storage ? VK_IMAGE_LAYOUT_GENERAL
	                                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

void Tiler::DetileImage(GraphicContext* ctx, DepthStencilVulkanImage* image,
                        const DepthTargetInfo& info, const BufferImageCopySource& source,
                        bool refresh, uint32_t base_layer) const {
	const bool d16 =
	    info.guest_format == Prospero::GpuEnumValue(Prospero::BufferFormat::k16UNorm) &&
	    info.format == VK_FORMAT_D16_UNORM && info.bytes_per_element == 2;
	const bool d32 =
	    info.guest_format == Prospero::GpuEnumValue(Prospero::BufferFormat::k32Float) &&
	    (info.format == VK_FORMAT_D32_SFLOAT || info.format == VK_FORMAT_D32_SFLOAT_S8_UINT) &&
	    info.bytes_per_element == 4;
	if (ctx == nullptr || image == nullptr || info.address == 0 || info.size == 0 ||
	    info.width == 0 || info.height == 0 || info.pitch < info.width || info.layers == 0 ||
	    info.size % info.layers != 0 || base_layer > image->layers ||
	    info.layers > image->layers - base_layer || info.size > UINT32_MAX ||
	    info.tile_mode != Prospero::GpuEnumValue(Prospero::TileMode::kDepth) || (!d16 && !d32) ||
	    !source.cpu_current || source.address != info.address || source.size != info.size ||
	    (source.buffer == nullptr && source.offset != 0)) {
		EXIT("Tiler: unsupported depth detile, ctx=%p image=%p source_buffer=%p "
		     "source=0x%016" PRIx64 "+0x%016" PRIx64 " offset=0x%016" PRIx64
		     " current=%d depth=0x%016" PRIx64 "+0x%016" PRIx64
		     " extent=%ux%u pitch=%u tile=%u format=%d guest_format=%u bpe=%u\n",
		     static_cast<const void*>(ctx), static_cast<const void*>(image),
		     static_cast<const void*>(source.buffer), source.address, source.size, source.offset,
		     source.cpu_current, info.address, info.size, info.width, info.height, info.pitch,
		     info.tile_mode, static_cast<int>(info.format), info.guest_format,
		     info.bytes_per_element);
	}
	if (refresh) {
		VulkanDeviceWaitIdle(ctx);
	}
	const auto                   slice_size = info.size / info.layers;
	UtilScratchBuffer            linear(info.size);
	std::vector<BufferImageCopy> regions;
	regions.reserve(info.layers);
	for (uint32_t layer = 0; layer < info.layers; layer++) {
		auto* linear_slice = static_cast<uint8_t*>(linear.Data()) + slice_size * layer;
		auto* guest_slice  = reinterpret_cast<const uint8_t*>(source.address) + slice_size * layer;
		TileConvertTiledToLinearDepth(linear_slice, guest_slice, info.guest_format, info.width,
		                              info.height, info.pitch, slice_size);
		BufferImageCopy region {};
		region.offset    = static_cast<uint32_t>(slice_size * layer);
		region.pitch     = info.pitch;
		region.width     = info.width;
		region.height    = info.height;
		region.dst_layer = base_layer + layer;
		region.aspect    = VK_IMAGE_ASPECT_DEPTH_BIT;
		regions.push_back(region);
	}
	UtilFillImage(ctx, image, linear.Data(), info.size, regions,
	              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void Tiler::DetileStencil(GraphicContext* ctx, DepthStencilVulkanImage* image,
                          const DepthTargetInfo& info, const BufferImageCopySource& source,
                          bool refresh, uint32_t base_layer) const {
	TileSizeAlign stencil_size {};
	TileSizeAlign htile_size {};
	TileSizeAlign depth_size {};
	const auto    stencil_format = Prospero::GpuEnumValue(Prospero::BufferFormat::k8UInt);
	const auto    stencil_pitch  = TileGetTexturePitch(
	    stencil_format, info.width, 1, Prospero::GpuEnumValue(Prospero::TileMode::kDepth));
	const bool supported_layout =
	    TileGetDepthSize(info.width, info.height, 0,
	                     Prospero::GpuEnumValue(Prospero::DepthFormat::kZ32F),
	                     Prospero::GpuEnumValue(Prospero::StencilFormat::k8UInt),
	                     info.htile_address != 0, &stencil_size, &htile_size, &depth_size) &&
	    info.layers != 0 && info.stencil_size % info.layers == 0 &&
	    stencil_size.size == info.stencil_size / info.layers && stencil_size.align == 65536 &&
	    stencil_pitch != 0;
	if (ctx == nullptr || image == nullptr || info.address == 0 || info.size == 0 ||
	    info.stencil_address == 0 || info.stencil_size == 0 || info.width == 0 ||
	    info.height == 0 || info.format != VK_FORMAT_D32_SFLOAT_S8_UINT ||
	    info.guest_format != Prospero::GpuEnumValue(Prospero::BufferFormat::k32Float) ||
	    info.bytes_per_element != 4 || base_layer > image->layers ||
	    info.layers > image->layers - base_layer || info.stencil_size > UINT32_MAX ||
	    info.tile_mode != Prospero::GpuEnumValue(Prospero::TileMode::kDepth) ||
	    info.stencil_htile_compressed || !supported_layout || !source.cpu_current ||
	    source.address != info.stencil_address || source.size != info.stencil_size ||
	    (source.buffer == nullptr && source.offset != 0)) {
		EXIT("Tiler: unsupported stencil detile, ctx=%p image=%p source_buffer=%p "
		     "source=0x%016" PRIx64 "+0x%016" PRIx64 " offset=0x%016" PRIx64
		     " current=%d stencil=0x%016" PRIx64 "+0x%016" PRIx64
		     " extent=%ux%u depth_pitch=%u stencil_pitch=%u tile=%u format=%d guest_format=%u "
		     "bpe=%u\n",
		     static_cast<const void*>(ctx), static_cast<const void*>(image),
		     static_cast<const void*>(source.buffer), source.address, source.size, source.offset,
		     source.cpu_current, info.stencil_address, info.stencil_size, info.width, info.height,
		     info.pitch, stencil_pitch, info.tile_mode, static_cast<int>(info.format),
		     info.guest_format, info.bytes_per_element);
	}
	if (refresh) {
		VulkanDeviceWaitIdle(ctx);
	}
	const auto                   slice_size = info.stencil_size / info.layers;
	UtilScratchBuffer            linear(info.stencil_size);
	std::vector<BufferImageCopy> regions;
	regions.reserve(info.layers);
	for (uint32_t layer = 0; layer < info.layers; layer++) {
		auto* linear_slice = static_cast<uint8_t*>(linear.Data()) + slice_size * layer;
		auto* guest_slice  = reinterpret_cast<const uint8_t*>(source.address) + slice_size * layer;
		TileConvertTiledToLinearDepth(linear_slice, guest_slice, stencil_format, info.width,
		                              info.height, stencil_pitch, slice_size);
		BufferImageCopy region {};
		region.offset    = static_cast<uint32_t>(slice_size * layer);
		region.pitch     = stencil_pitch;
		region.width     = info.width;
		region.height    = info.height;
		region.dst_layer = base_layer + layer;
		region.aspect    = VK_IMAGE_ASPECT_STENCIL_BIT;
		regions.push_back(region);
	}
	UtilFillImage(ctx, image, linear.Data(), info.stencil_size, regions,
	              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void Tiler::TileImage(void* dst, const void* src, const RenderTargetInfo& info) const {
	if (dst == nullptr || src == nullptr || info.address == 0 || info.size == 0 ||
	    info.width == 0 || info.height == 0 || info.pitch < info.width ||
	    info.tile_mode != Prospero::GpuEnumValue(Prospero::TileMode::kRenderTarget) ||
	    info.levels != 1 || info.layers == 0 || info.size % info.layers != 0 ||
	    !IsSupportedRenderTargetElementSize(info.bytes_per_element)) {
		EXIT("Tiler: unsupported render-target tile, dst=%p src=%p "
		     "addr=0x%016" PRIx64 "+0x%016" PRIx64
		     " extent=%ux%u pitch=%u levels=%u tile=%u bpe=%u\n",
		     dst, src, info.address, info.size, info.width, info.height, info.pitch, info.levels,
		     info.tile_mode, info.bytes_per_element);
	}
	const auto slice_size = info.size / info.layers;
	for (uint32_t layer = 0; layer < info.layers; layer++) {
		auto* guest_slice  = static_cast<uint8_t*>(dst) + slice_size * layer;
		auto* linear_slice = static_cast<const uint8_t*>(src) + slice_size * layer;
		TileConvertLinearToTiledRenderTarget(guest_slice, linear_slice, info.width, info.height,
		                                     info.pitch, info.bytes_per_element, slice_size);
	}
}

void Tiler::TileImage(void* dst, const void* src, const DepthTargetInfo& info) const {
	const bool supported_format =
	    (info.guest_format == Prospero::GpuEnumValue(Prospero::BufferFormat::k16UNorm) &&
	     info.format == VK_FORMAT_D16_UNORM && info.bytes_per_element == 2) ||
	    (info.guest_format == Prospero::GpuEnumValue(Prospero::BufferFormat::k32Float) &&
	     info.format == VK_FORMAT_D32_SFLOAT && info.bytes_per_element == 4);
	if (dst == nullptr || src == nullptr || info.address == 0 || info.size == 0 ||
	    info.stencil_address != 0 || info.stencil_size != 0 || info.width == 0 ||
	    info.height == 0 || info.pitch < info.width ||
	    info.tile_mode != Prospero::GpuEnumValue(Prospero::TileMode::kDepth) || info.layers == 0 ||
	    info.size % info.layers != 0 || !supported_format) {
		EXIT("Tiler: unsupported depth-target tile, dst=%p src=%p "
		     "depth=0x%016" PRIx64 "+0x%016" PRIx64 " stencil=0x%016" PRIx64 "+0x%016" PRIx64
		     " extent=%ux%u pitch=%u tile=%u format=%d guest_format=%u bpe=%u\n",
		     dst, src, info.address, info.size, info.stencil_address, info.stencil_size, info.width,
		     info.height, info.pitch, info.tile_mode, static_cast<int>(info.format),
		     info.guest_format, info.bytes_per_element);
	}
	const auto slice_size = info.size / info.layers;
	for (uint32_t layer = 0; layer < info.layers; layer++) {
		auto* guest_slice  = static_cast<uint8_t*>(dst) + slice_size * layer;
		auto* linear_slice = static_cast<const uint8_t*>(src) + slice_size * layer;
		TileConvertLinearToTiledDepth(guest_slice, linear_slice, info.guest_format, info.width,
		                              info.height, info.pitch, slice_size);
	}
}

} // namespace Libs::Graphics
