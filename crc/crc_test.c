#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "crc32poly.h"

uint32_t crc32_le_base(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32_be_base(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32c_base(uint32_t crc, const uint8_t *data, size_t len);


int main(int argc, char** argv) {
    size_t buffer_lens[] = {7, 17, 127, 2049};

    for (int i = 0; i < sizeof(buffer_lens) / sizeof(size_t); i++) {
        uint8_t* buffer = malloc(buffer_lens[i]);
        if (buffer == NULL) {
            printf("malloc failed\n");
            return 1;
        }
        // randomizing buffer content
        for (int j = 0; j < buffer_lens[i]; j++) {
            buffer[j] = (uint8_t)rand();
        }
        // evaluating CRC32
        uint32_t crc_le = crc32_le_base(0, buffer, buffer_lens[i]);
        uint32_t crc_be = crc32_be_base(0, buffer, buffer_lens[i]);
        uint32_t crc_c = crc32c_base(0, buffer, buffer_lens[i]);
        printf("Buffer length: %zu\n", buffer_lens[i]);
        printf("CRC32 LE: 0x%x\n", crc_le);
        printf("CRC32 BE: 0x%x\n", crc_be);
        printf("CRC32 C: 0x%x\n", crc_c);
        free(buffer);
    }

    return 0;
}
