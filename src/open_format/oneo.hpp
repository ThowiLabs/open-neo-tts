#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace openneo {
struct Metadata {
  std::string name,author,description,license,version,url;
  std::filesystem::path cover;
};
struct Model {
  std::string config,metadata;
  std::vector<unsigned char> onnx,cover;
  std::uint32_t format_version=1;
};
// Legacy ONEO v1 remains readable. New packages use ONEO v2.
Model load(const std::filesystem::path&);
void pack(const std::filesystem::path& onnx,const std::filesystem::path& config,
          const std::filesystem::path& output,const Metadata& metadata={});
}
