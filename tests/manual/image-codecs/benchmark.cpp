#include <miniz.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>
#include <webp/encode.h>
#include <webp/decode.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
static unsigned char *png_compress(unsigned char *data, int size, int *output, int)
{
	mz_ulong n = mz_compressBound(size);
	auto *p = static_cast<unsigned char *>(malloc(n));
	if (!p || mz_compress2(p, &n, data, size, 9) != MZ_OK)
		throw std::runtime_error("PNG compression");
	*output = static_cast<int>(n);
	return p;
}
#define STBIW_ZLIB_COMPRESS png_compress
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
using Bytes = std::vector<unsigned char>;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
Bytes read(const std::filesystem::path &p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f)
		throw std::runtime_error("read " + p.string());
	return {std::istreambuf_iterator<char>(f), {}};
}
void write(const std::filesystem::path &p, const Bytes &b)
{
	std::ofstream f(p, std::ios::binary);
	f.write(reinterpret_cast<const char *>(b.data()), b.size());
	if (!f)
		throw std::runtime_error("write");
}
Bytes png(const Bytes &b, int &w, int &h)
{
	int c;
	auto *p = stbi_load_from_memory(b.data(), static_cast<int>(b.size()), &w, &h, &c, 4);
	if (!p)
		throw std::runtime_error("PNG decode");
	Bytes out(p, p + size_t(w) * h * 4);
	stbi_image_free(p);
	return out;
}
Bytes zip(const Bytes &b, int level)
{
	mz_zip_archive z{};
	if (!mz_zip_writer_init_heap(&z, 0, 0) || !mz_zip_writer_add_mem(&z, "image", b.data(), b.size(), level))
		throw std::runtime_error("zip");
	void *p;
	size_t n;
	if (!mz_zip_writer_finalize_heap_archive(&z, &p, &n))
		throw std::runtime_error("zip end");
	Bytes out(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + n);
	mz_free(p);
	mz_zip_writer_end(&z);
	return out;
}
Bytes unzip(const Bytes &b)
{
	mz_zip_archive z{};
	if (!mz_zip_reader_init_mem(&z, b.data(), b.size(), 0))
		throw std::runtime_error("zip read");
	size_t n;
	auto *p = static_cast<unsigned char *>(mz_zip_reader_extract_file_to_heap(&z, "image", &n, 0));
	if (!p)
		throw std::runtime_error("inflate");
	Bytes out(p, p + n);
	mz_free(p);
	mz_zip_reader_end(&z);
	return out;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 4)
			throw std::runtime_error(
			    "usage: image_codecs encode|decode|rewrap|compare source-directory corpus-directory");
		const std::filesystem::path source = argv[2], root = argv[3];
		if (std::string(argv[1]) == "compare")
		{
			const auto native = Json::parse(read(source / "manifest.json"));
			const auto wasm = Json::parse(read(root / "manifest.json"));
			if (native != wasm)
				throw std::runtime_error("Native/WASM corpus differs");
			for (const auto &row : native)
			{
				const auto key = row.at("key").get<std::string>();
				if (read(source / (key + ".rgba")) != read(root / (key + ".rgba")) ||
				    unzip(read(source / (key + ".webp"))) != unzip(read(root / (key + ".webp"))))
					throw std::runtime_error("Native/WASM pixels or encoding differ: " +
					                         row.at("file").get<std::string>());
			}
			std::cout << "PASS native/WASM pixels and WebP records identical for " << native.size() << " images\n";
			return 0;
		}
		const std::vector<std::string> modes = {"raw1", "raw9", "png9", "webp"};
		if (std::string(argv[1]) == "encode")
		{
			std::filesystem::create_directories(root);
			std::vector<std::filesystem::path> files;
			for (const auto &e : std::filesystem::recursive_directory_iterator(source))
				if (e.path().extension() == ".png")
					files.push_back(e.path());
			std::sort(files.begin(), files.end());
			Json manifest = Json::array();
			Json encode_ms = Json::object();
			size_t original = 0;
			for (size_t i = 0; i < files.size(); ++i)
			{
				auto input = read(files[i]);
				original += input.size();
				int w, h;
				auto pixels = png(input, w, h);
				const auto key = std::to_string(i);
				write(root / (key + ".rgba"), pixels);
				Json row = {{"file", files[i].lexically_relative(source).generic_string()},
				            {"key", key},
				            {"width", w},
				            {"height", h}};
				for (const auto &mode : modes)
				{
					const auto start = Clock::now();
					Bytes data;
					if (mode.starts_with("raw"))
					{
						data = {'P', 'H', 'X', 'R', 'G', 'B', 'A', '1'};
						for (int v : {w, h})
							for (int j = 0; j < 4; ++j)
								data.push_back(static_cast<unsigned char>(v >> (j * 8)));
						data.insert(data.end(), pixels.begin(), pixels.end());
					}
					else if (mode == "png9")
					{
						stbi_write_png_compression_level = 9;
						if (!stbi_write_png_to_func(
						        [](void *c, void *p, int n)
						        {
							        auto &b = *static_cast<Bytes *>(c);
							        b.insert(b.end(), static_cast<unsigned char *>(p),
							                 static_cast<unsigned char *>(p) + n);
						        },
						        &data, w, h, 4, pixels.data(), w * 4))
							throw std::runtime_error("PNG encode");
						if (input.size() < data.size())
							data = input;
					}
					else
					{
						WebPConfig config;
						if (!WebPConfigInit(&config))
							throw std::runtime_error("config");
						config.lossless = 1;
						config.exact = 1;
						config.method = 6;
						config.quality = 100;
						config.thread_level = 0;
						WebPPicture pic;
						WebPPictureInit(&pic);
						pic.use_argb = 1;
						pic.width = w;
						pic.height = h;
						WebPMemoryWriter writer;
						WebPMemoryWriterInit(&writer);
						pic.writer = WebPMemoryWrite;
						pic.custom_ptr = &writer;
						if (!WebPPictureImportRGBA(&pic, pixels.data(), w * 4) || !WebPEncode(&config, &pic))
							throw std::runtime_error("WebP encode");
						data = {'P', 'H', 'X', 'W', 'E', 'B', 'P', '1'};
						for (int v : {w, h})
							for (int j = 0; j < 4; ++j)
								data.push_back(static_cast<unsigned char>(v >> (j * 8)));
						data.insert(data.end(), writer.mem, writer.mem + writer.size);
						WebPMemoryWriterClear(&writer);
						WebPPictureFree(&pic);
					}
					auto archive = zip(data, mode == "raw1" ? 1 : mode == "raw9" ? 9 : 0);
					write(root / (key + "." + mode), archive);
					encode_ms[mode] = encode_ms.value(mode, 0.0) +
					                  std::chrono::duration<double, std::milli>(Clock::now() - start).count();
				}
				manifest.push_back(row);
				std::cerr << i + 1 << "/" << files.size() << " " << row["file"] << "\n";
			}
			std::ofstream(root / "manifest.json") << manifest.dump(2);
			std::ofstream(root / "encoding.json")
			    << Json({{"original_png_bytes", original}, {"encode_ms", encode_ms}}).dump(2) << "\n";
		}
		auto manifest = Json::parse(read(root / "manifest.json"));
		if (std::string(argv[1]) == "rewrap")
			for (const auto &row : manifest)
				for (const auto &mode : {"png9", "webp"})
				{
					const auto file = root / (row.at("key").get<std::string>() + "." + mode);
					write(file, zip(unzip(read(file)), 0));
				}
		Json results;
		for (const auto &mode : std::vector<std::string>{"raw1", "raw9", "png9", "webp", "adaptive"})
		{
			size_t bytes = 0;
			std::vector<double> passes(3, 0);
			for (const auto &row : manifest)
			{
				const auto key = row.at("key").get<std::string>();
				auto codec = mode;
				if (codec == "adaptive")
					codec = std::filesystem::file_size(root / (key + ".webp")) <
					                std::filesystem::file_size(root / (key + ".png9"))
					            ? "webp"
					            : "png9";
				auto encoded = read(root / (key + "." + codec));
				auto expected = read(root / (key + ".rgba"));
				bytes += encoded.size();
				for (int pass = 0; pass < 3; ++pass)
				{
					auto start = Clock::now();
					auto data = unzip(encoded);
					Bytes actual;
					int w = row["width"], h = row["height"];
					if (codec.starts_with("raw"))
						actual.assign(data.begin() + 16, data.end());
					else if (codec == "png9")
						actual = png(data, w, h);
					else
					{
						WebPBitstreamFeatures features{};
						if (WebPGetFeatures(data.data() + 16, data.size() - 16, &features) != VP8_STATUS_OK ||
						    features.format != 2 || features.has_animation || features.width != w ||
						    features.height != h)
							throw std::runtime_error("WebP features");
						actual.resize(size_t(w) * h * 4);
						if (!WebPDecodeRGBAInto(data.data() + 16, data.size() - 16, actual.data(), actual.size(),
						                        w * 4))
							throw std::runtime_error("WebP decode");
					}
					passes[pass] += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
					if (w != row["width"] || h != row["height"] || actual != expected)
						throw std::runtime_error("PIXEL MISMATCH " + key + " " + mode);
				}
			}
			results[mode] = {{"archive_bytes", bytes}, {"decode_ms", passes}, {"identical_pixels", true}};
		}
		std::cout << Json({{"images", manifest.size()}, {"results", results}}).dump(2) << "\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
