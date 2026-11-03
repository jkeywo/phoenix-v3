module;
#include <set>
#include <cstdint>
#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.image_codec;
export import phoenix.data;

export namespace phoenix
{
struct AssetImage
{
	int width = 0, height = 0;
	Bytes rgba;
};
bool compiled_image(std::span<const std::uint8_t> bytes);
AssetImage decode_asset_image(std::span<const std::uint8_t> bytes);
// Immutable pixels can be retained by prepared graphics owners, so material
// validation and CPU sampling reuse worker decoding instead of decoding again.
std::shared_ptr<const AssetImage> shared_asset_image(std::span<const std::uint8_t> bytes);
// Fully validates pixels, reusing bounded success receipts for identical bytes.
std::array<int, 2> verified_image_dimensions(std::span<const std::uint8_t> bytes);
// Retain successful image checks by exact byte identity for one catalogue pass.
class ImageVerificationScope
{
public:
	ImageVerificationScope();
	~ImageVerificationScope();
	ImageVerificationScope(const ImageVerificationScope &) = delete;
	ImageVerificationScope &operator=(const ImageVerificationScope &) = delete;

private:
	friend std::array<int, 2> verified_image_dimensions(std::span<const std::uint8_t>);
	static thread_local ImageVerificationScope *active_;
	ImageVerificationScope *previous_;
	std::map<std::string, std::array<int, 2>, std::less<>> dimensions_;
	std::size_t retained_bytes_ = 0;
};
Bytes compile_asset_image(std::span<const std::uint8_t> bytes);
// Standard lossless WebP bytes for browser metadata, without the runtime container.
Bytes browser_asset_image(std::span<const std::uint8_t> bytes);
Bytes source_asset_image(std::span<const std::uint8_t> bytes);
Bytes source_asset_image(const AssetImage &image);
} // namespace phoenix
