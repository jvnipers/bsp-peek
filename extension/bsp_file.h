#ifndef _INCLUDE_BSPPEEK_BSP_FILE_H_
#define _INCLUDE_BSPPEEK_BSP_FILE_H_

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// The .bsp file on disk: its header and lumps. Callers pick the lumps and what a bad one means.
namespace BSPFile
{
	constexpr int kIdent = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'V');
	constexpr int kNumLumps = 64;
	constexpr int kHeaderSize = 1036; // ident, version, kNumLumps lump entries of 16 bytes, map revision

	struct Lump
	{
		int32_t fileofs;
		int32_t filelen;
		int32_t version;
		int32_t uncompressedLen; // nonzero: LZMA-compressed
	};

	struct Header
	{
		uint8_t raw[kHeaderSize];

		int32_t Field(int off) const
		{
			int32_t v;
			std::memcpy(&v, raw + off, 4);
			return v;
		}

		int32_t Ident() const
		{
			return Field(0);
		}

		int32_t Version() const
		{
			return Field(4);
		}

		int32_t Revision() const
		{
			return Field(8 + kNumLumps * 16);
		}

		Lump LumpAt(int i) const
		{
			int base = 8 + i * 16;
			return {Field(base), Field(base + 4), Field(base + 8), Field(base + 12)};
		}
	};

	// From a file just opened. False on a short read.
	inline bool ReadHeader(FILE *f, Header &h)
	{
		return std::fread(h.raw, 1, kHeaderSize, f) == (size_t)kHeaderSize;
	}

	// len bytes at offset into out, empty for len <= 0. False with out empty when the file is shorter or the read fails.
	inline bool Read(FILE *f, int64_t offset, int len, std::vector<uint8_t> &out)
	{
		out.clear();
		if (len <= 0)
		{
			return true;
		}
		// Bound by the file size before allocating: a corrupt header length would throw bad_alloc.
		if (offset < 0 || std::fseek(f, 0, SEEK_END) != 0 || std::ftell(f) - offset < len)
		{
			return false;
		}
		out.resize(len);
		if (std::fseek(f, (long)offset, SEEK_SET) != 0 || std::fread(out.data(), 1, len, f) != (size_t)len)
		{
			out.clear();
			return false;
		}
		return true;
	}
} // namespace BSPFile

#endif
