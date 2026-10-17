#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace neo::vits {
struct TextChunk {
  std::string text;
  int pause_after_ms=0;
};

// Piper-inspired, UTF-8-safe smart chunking. The entire document has no
// phoneme-count limit; each chunk is phonemized and synthesized separately.
// Prioritize paragraph / sentence / clause / whitespace, preserving order.
std::vector<TextChunk> split_text_for_speech(
    const std::string& input,std::size_t preferred_bytes=150);
} // namespace neo::vits
