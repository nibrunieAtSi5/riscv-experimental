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
uint32_t rv_crc32_le_vector_clmul(uint32_t crc, unsigned char const *p, size_t len);
uint32_t rv_crc32_le_vector_clmul_fold(uint32_t crc, unsigned char const *p, size_t len); 
uint32_t rv_crc32_le_vector_clmul_fold_asm(uint32_t crc, unsigned char const *p, size_t len); 
#endif // defined(__riscv) 

static inline uint64_t get_cycles() {
#if defined(__riscv)
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;
#elif defined(__x86_64__)
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));
    return val;
#else
    return 0;
#endif
}

#define MEASURE(NAME, REF, FUNC, ...) \
    do { \
        uint64_t start = get_cycles(); \
        uint32_t res = FUNC(__VA_ARGS__); \
        uint64_t end = get_cycles(); \
        printf("%-36s: 0x%08x (%" PRIu64 " cycles)\n", NAME, res, end - start); \
        if (res != REF) {\
            printf("Error: %s: 0x%08x != 0x%08x\n", NAME, res, REF); \
            return 1; \
        }\
    } while (0)

int main(int argc, char** argv) {
    size_t buffer_lens[] = {7, 8, 15, 16, 17, 32, 128, 2047, 2048, 2049};

    for (int i = 0; i < sizeof(buffer_lens) / sizeof(size_t); i++) {
        uint8_t* buffer = malloc(buffer_lens[i]);
        if (buffer == NULL) {
            printf("malloc failed\n");
            return 1;
        }
        // randomizing buffer content
        for (int j = 0; j < buffer_lens[i]; j++) {
            buffer[j] = (j == 0) ? 0x1 : 0; //  (uint8_t) ((j * 3) % 256) : ((uint8_t) ((j * 3) % 256)); //rand();
        }
        
        printf("==========================================\n");
        printf("Buffer length: %zu\n", buffer_lens[i]);

        uint32_t ref_crc32_be = crc32_be_base(0, buffer, buffer_lens[i]);
        uint32_t ref_crc32_le = crc32_le_base(0, buffer, buffer_lens[i]);
        uint32_t ref_crc32c = crc32c_base(0, buffer, buffer_lens[i]);
        
        MEASURE("CRC32 BE", ref_crc32_be, crc32_be_base, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 LE", ref_crc32_le, crc32_le_base, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 LE generic", ref_crc32_le, crc32_le_generic, 0, buffer, buffer_lens[i]);
#if defined(__riscv)
        MEASURE("CRC32 RV LE", ref_crc32_le, rv_crc32_le, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 RV LE opt", ref_crc32_le, rv_crc32_le_opt, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 RV LE vector clmul", ref_crc32_le, rv_crc32_le_vector_clmul, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 RV LE vector clmul fold (C)", ref_crc32_le, rv_crc32_le_vector_clmul_fold, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32 RV LE vector clmul fold (asm)", ref_crc32_le, rv_crc32_le_vector_clmul_fold_asm, 0, buffer, buffer_lens[i]);
        MEASURE("CRC32C RV LE", ref_crc32c, rv_crc32c_le, 0, buffer, buffer_lens[i]);
#endif // defined(__riscv) 

        MEASURE("CRC32C LE", ref_crc32c, crc32c_base, 0, buffer, buffer_lens[i]);
        free(buffer);
    }

    return 0;
}
