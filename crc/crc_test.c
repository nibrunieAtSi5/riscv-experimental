#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "crc32poly.h"

uint32_t crc32_le_base(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32_be_base(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32c_base(uint32_t crc, const uint8_t *data, size_t len);

uint32_t crc32_le_generic(uint32_t crc, unsigned char const *p, size_t len);


#if defined(__riscv)
uint32_t rv_crc32_le(uint32_t crc, const uint8_t *data, size_t len);
uint32_t rv_crc32c_le(uint32_t crc, const uint8_t *data, size_t len);
uint32_t rv_crc32_le_opt(uint32_t crc, const uint8_t *buffer, size_t len);
#endif // defined(__riscv) 


int main(int argc, char** argv) {
    size_t buffer_lens[] = {7, 8, 15, 16, 17, 32, 31, 33, 127, 2049};

    for (int i = 0; i < sizeof(buffer_lens) / sizeof(size_t); i++) {
        uint8_t* buffer = malloc(buffer_lens[i]);
        if (buffer == NULL) {
            printf("malloc failed\n");
            return 1;
        }
        // randomizing buffer content
        for (int j = 0; j < buffer_lens[i]; j++) {
            buffer[j] = (uint8_t) ((j * 3) % 256); //rand();
        }
        // evaluating CRC32
        uint32_t crc_le = crc32_le_base(0, buffer, buffer_lens[i]);
        uint32_t crc_be = crc32_be_base(0, buffer, buffer_lens[i]);
        uint32_t crc_c = crc32c_base(0, buffer, buffer_lens[i]);

        uint32_t crc_le_generic = crc32_le_generic(0, buffer, buffer_lens[i]);
        printf("Buffer length: %zu\n", buffer_lens[i]);
        printf("CRC32 BE: 0x%x\n", crc_be);
        printf("CRC32 LE: 0x%x\n", crc_le);
        printf("CRC32 LE generic: 0x%x\n", crc_le_generic);
#if defined(__riscv)
        uint32_t crc_rv_le = rv_crc32_le(0, buffer, buffer_lens[i]);
        printf("CRC32 RV LE:      0x%"PRIx32"\n", crc_rv_le);
        uint32_t crc_rv_le_opt = rv_crc32_le_opt(0, buffer, buffer_lens[i]);
        printf("CRC32 RV LE opt:  0x%"PRIx32"\n", crc_rv_le_opt);
#endif // defined(__riscv) 
        printf("CRC32C LE:    0x%x\n", crc_c);
#if defined(__riscv)
        uint32_t crc_rv_c = rv_crc32c_le(0, buffer, buffer_lens[i]);
        printf("CRC32C RV LE: 0x%x\n", crc_rv_c);
#endif // defined(__riscv)
        free(buffer);
    }

    return 0;
}
