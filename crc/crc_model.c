#include <inttypes.h>
#include <stddef.h>


// Copied from https://github.com/torvalds/linux/blob/66498c75b4f8017f62d720d9b59675bdf3abce91/lib/crc/crc32-main.c
#include "crc32table.h"

#define u32 uint32_t
#define u8 uint8_t
#define __maybe_unused

/*static inline*/ u32 __maybe_unused
crc32_le_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32table_le[(crc & 255) ^ *p++];
	return crc;
}

/*static inline*/ u32 __maybe_unused
crc32_be_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc << 8) ^ crc32table_be[(crc >> 24) ^ *p++];
	return crc;
}

/*static inline*/ u32 __maybe_unused
crc32c_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32ctable_le[(crc & 255) ^ *p++];
	return crc;
}