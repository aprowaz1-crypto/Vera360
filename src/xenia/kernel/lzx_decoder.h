/**
 * Vera360 — Xenia Edge
 * LZX Decoder — decompresses Xbox 360 XEX2 LZX-compressed images
 *
 * Xbox 360 uses a custom LZX variant for executable compression.
 * The format stores data in blocks with a block descriptor table:
 *   - Each block has a SHA-1 hash (20 bytes) of the NEXT block
 *   - First block hash is in the file format info header
 *   - Data is LZX-compressed with window_bits=17 (128KB window)
 *
 * The LZX algorithm uses three Huffman trees:
 *   - Main: literals (0-255) + match lengths
 *   - Length: additional match length bits
 *   - Aligned offset: for aligned-offset blocks
 *
 * Reference: Microsoft CAB LZX specification + Xenia src
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace xe::kernel {

/// Decompress XEX2 LZX-compressed data
/// @param compressed_data  Pointer to compressed data (after XEX2 header)
/// @param compressed_size  Size of compressed data
/// @param uncompressed_size Expected output size (module_.image_size)
/// @param window_bits      LZX window size bits (typically 17 for Xbox 360)
/// @param block_descriptors Array of block sizes from the XEX2 format info
/// @param output           Output vector to store decompressed data
/// @return true on success
bool LzxDecompress(const uint8_t* compressed_data,
                   size_t compressed_size,
                   size_t uncompressed_size,
                   uint32_t window_bits,
                   const std::vector<uint32_t>& block_sizes,
                   std::vector<uint8_t>& output);

/// Simpler XEX2-specific variant: handles the block chain with SHA-1 hashes
/// Each compressed block is preceded by a 24-byte header:
///   [0-3] = block data size (big-endian)
///   [4-23] = SHA-1 hash of next block
bool LzxDecompressXex(const uint8_t* data,
                      size_t data_size,
                      size_t uncompressed_size,
                      uint32_t window_bits,
                      uint32_t first_block_size,
                      std::vector<uint8_t>& output);

}  // namespace xe::kernel
