module;
#include <webp/decode.h>
#include <webp/encode.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <vector>
module phoenix.content.image_codec;
namespace phoenix
{
namespace
{
constexpr std::size_t max_file = 16 * 1024 * 1024;
constexpr std::string_view webp_magic = "PHXWEBP1";
bool signature(std::span<const std::uint8_t> bytes, std::string_view magic)
{
	return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
}
} // namespace
bool compiled_image(std::span<const std::uint8_t> bytes)
{
	return signature(bytes, webp_magic);
}
AssetImage decode_asset_image(std::span<const std::uint8_t> bytes)
{
	AssetImage result;
	if (compiled_image(bytes))
	{
		if (bytes.size() < 16)
			throw std::runtime_error("image: truncated compiled pixels");
		auto integer = [&](std::size_t at)
		{
			std::uint32_t value = 0;
			for (unsigned i = 0; i < 4; ++i)
				value |= std::uint32_t(bytes[at + i]) << (i * 8);
			return value;
		};
		const auto w = integer(8), h = integer(12);
		if (!w || !h || w > 8192 || h > 8192 || std::uint64_t(w) * h > 16777216)
			throw std::runtime_error("image: invalid or oversized compiled dimensions");
		result.width = static_cast<int>(w);
		result.height = static_cast<int>(h);
		const auto payload = bytes.subspan(16);
		WebPBitstreamFeatures features{};
		if (WebPGetFeatures(payload.data(), payload.size(), &features) != VP8_STATUS_OK || features.format != 2 ||
		    features.has_animation || features.width != result.width || features.height != result.height)
			throw std::runtime_error("image: invalid lossless WebP dimensions or encoding");
		result.rgba.resize(std::size_t(w) * h * 4);
		if (!WebPDecodeRGBAInto(payload.data(), payload.size(), result.rgba.data(), result.rgba.size(),
		                        result.width * 4))
			throw std::runtime_error("image: WebP cannot decode");
		return result;
	}
	if (signature(bytes, "PHX"))
		throw std::runtime_error("image: unsupported compiled image version; rebuild the DAT");
	int channels = 0;
	if (bytes.size() > max_file ||
	    !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &result.width, &result.height,
	                           &channels) ||
	    result.width <= 0 || result.height <= 0 || result.width > 8192 || result.height > 8192 ||
	    std::int64_t(result.width) * result.height > 16777216)
		throw std::runtime_error("image: invalid or oversized PNG dimensions");
	auto *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &result.width, &result.height,
	                                     &channels, 4);
	if (!pixels)
		throw std::runtime_error("image: PNG cannot decode");
	result.rgba.assign(pixels, pixels + std::size_t(result.width) * result.height * 4);
	stbi_image_free(pixels);
	return result;
}
std::array<int, 2> verified_image_dimensions(std::span<const std::uint8_t> bytes)
{
	// Never key validation by a mutable asset path: edited replacements need a new
	// full decode. Compare exact encoded bytes, avoiding repeated cryptographic
	// hashing on the graphics thread. No decoded pixel buffers are retained.
	struct Receipt
	{
		Bytes encoded;
		std::array<int, 2> dimensions;
	};
	struct Cache
	{
		std::vector<Receipt> receipts;
		std::size_t size = 0;
	};
	thread_local Cache verified;
	for (const auto &receipt : verified.receipts)
		if (receipt.encoded.size() == bytes.size() && std::equal(bytes.begin(), bytes.end(), receipt.encoded.begin()))
			return receipt.dimensions;
	const auto image = decode_asset_image(bytes);
	const std::array dimensions{image.width, image.height};
	constexpr std::size_t budget = 32 * 1024 * 1024;
	if (bytes.size() <= budget)
	{
		if (verified.receipts.size() >= 128 || bytes.size() > budget - verified.size)
		{
			verified.receipts.clear();
			verified.size = 0;
		}
		verified.receipts.push_back({Bytes(bytes.begin(), bytes.end()), dimensions});
		verified.size += bytes.size();
	}
	return dimensions;
}
Bytes compile_asset_image(std::span<const std::uint8_t> bytes)
{
	struct Receipt
	{
		Bytes source, encoded;
	};
	struct Cache
	{
		std::vector<Receipt> receipts;
		std::size_t size = 0;
	};
	thread_local Cache cache;
	for (const auto &receipt : cache.receipts)
		if (receipt.source.size() == bytes.size() && std::equal(bytes.begin(), bytes.end(), receipt.source.begin()))
			return receipt.encoded;
	const auto image = decode_asset_image(bytes);
	WebPConfig config;
	if (!WebPConfigInit(&config))
		throw std::runtime_error("image: WebP configuration failed");
	config.lossless = 1;
	config.exact = 1; // Preserve even RGB under alpha=0, including normal/depth data.
	config.method = 6;
	config.quality = 100;
	config.thread_level = 0;
	WebPPicture picture;
	if (!WebPPictureInit(&picture))
		throw std::runtime_error("image: WebP picture failed");
	picture.use_argb = 1;
	picture.width = image.width;
	picture.height = image.height;
	WebPMemoryWriter writer;
	WebPMemoryWriterInit(&writer);
	struct Cleanup
	{
		WebPPicture *picture;
		WebPMemoryWriter *writer;
		~Cleanup()
		{
			WebPPictureFree(picture);
			WebPMemoryWriterClear(writer);
		}
	} cleanup{&picture, &writer};
	picture.writer = WebPMemoryWrite;
	picture.custom_ptr = &writer;
	if (!WebPPictureImportRGBA(&picture, image.rgba.data(), image.width * 4) || !WebPEncode(&config, &picture))
		throw std::runtime_error("image: lossless WebP encoding failed");
	Bytes result(webp_magic.begin(), webp_magic.end());
	for (const auto dimension : {image.width, image.height})
		for (unsigned i = 0; i < 4; ++i)
			result.push_back(static_cast<std::uint8_t>((dimension >> (i * 8)) & 255));
	result.insert(result.end(), writer.mem, writer.mem + writer.size);
	// Packaging must fail rather than publish a codec regression or changed pixel.
	const auto verified = decode_asset_image(result);
	if (verified.width != image.width || verified.height != image.height || verified.rgba != image.rgba)
		throw std::runtime_error("image: lossless WebP pixel verification failed");
	constexpr std::size_t budget = 32 * 1024 * 1024;
	const auto size = bytes.size() + result.size();
	if (size <= budget)
	{
		if (cache.receipts.size() >= 128 || size > budget - cache.size)
		{
			cache.receipts.clear();
			cache.size = 0;
		}
		cache.receipts.push_back({Bytes(bytes.begin(), bytes.end()), result});
		cache.size += size;
	}
	return result;
}

Bytes source_asset_image(std::span<const std::uint8_t> bytes)
{
	if (!compiled_image(bytes))
		return Bytes(bytes.begin(), bytes.end());
	return source_asset_image(decode_asset_image(bytes));
}
Bytes source_asset_image(const AssetImage &image)
{
	if (image.width <= 0 || image.height <= 0 || image.width > 8192 || image.height > 8192 ||
	    std::int64_t(image.width) * image.height > 16777216 ||
	    image.rgba.size() != std::size_t(image.width) * image.height * 4)
		throw std::runtime_error("image: invalid PNG source dimensions");
	Bytes result;
	if (!stbi_write_png_to_func(
	        [](void *context, void *data, int size)
	        {
		        auto &output = *static_cast<Bytes *>(context);
		        const auto *begin = static_cast<const std::uint8_t *>(data);
		        output.insert(output.end(), begin, begin + size);
	        },
	        &result, image.width, image.height, 4, image.rgba.data(), image.width * 4))
		throw std::runtime_error("image: PNG encoding failed");
	return result;
}

} // namespace phoenix
