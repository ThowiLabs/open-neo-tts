#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace neo::phonemes {
struct VoiceConfig {
  std::string language="es";
  std::map<std::string,std::vector<int64_t>> ids;
  std::map<std::string,std::vector<std::string>> remap;
};
struct Result {
  std::string ipa;
  std::vector<int64_t> ids;
  size_t unknown=0;
};
// Reads standard Piper voice JSON config (espeak.voice + phoneme_id_map).
VoiceConfig read_voice_config(const std::string& path);
// Parse the same Piper JSON from an in-memory signed Neo package.
VoiceConfig read_voice_config_json(const std::string& raw);
// eSpeak NG C API; path = parent directory containing espeak-ng-data.
// If path is empty, try packaged development location or system default.
// Thread safe for C++ callers sharing the global eSpeak instance.
Result from_text(const std::string& utf8, const VoiceConfig& voice,
                 const std::string& data_parent="", bool strict=false);
} // namespace neo::phonemes
