#include <stdint.h>
#include <stddef.h>
#include <stdio.h>


// vcrc32.vs       31..26=0x28 vm vs2 19..15=0x14 14..12=0x2 vd 6..0=0x77
// vcrc32c.vs      31..26=0x28 vm vs2 19..15=0x15 14..12=0x2 vd 6..0=0x77

// vcrc32.vs vd, vs2, vs1 (encoded as rs1=0x14 for vcrc32, rs1=0x15 for vcrc32c)
// opcode=0x77, func3=0x2, func7=(0x28<<1 | 1)=0x51 (vm=1 unmasked)
// Using inline assembly with .insn r

uint32_t rv_crc32_le(uint32_t crc, const uint8_t *buffer, size_t len) {

    size_t avl = len;
    uint32_t tmp_crc = crc;
    for (; avl > 0; ) {
        size_t vl = 0;

        // Example wrapper: you'll likely need to load/store vector registers.
        // Assuming v0 and v1 are used for vd and vs2 here.
        asm volatile (
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.v.x v2, %[crc] \n\t"
            "vsetvli %[vl], %[avl], e8, m1, ta, ma \n\t"
            "vle8.v v1, (%[buffer]) \n\t"
            ".insn r 0x77, 0x2, 0x51, x2, x20, x1 \n\t"
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.x.s %[crc], v2 \n\t"
            : [vl] "=r" (vl), [crc] "+r" (tmp_crc)
            : [avl] "r" (avl), [buffer] "r" (buffer)
            : "v0", "v1", "v2", "memory", "cc"
        );
        // printf("  vl=%d\n", vl);
        // printf("  crc=%x\n", tmp_crc);
        avl -= vl;
        buffer += vl;

    }

    return tmp_crc;
}

uint32_t rv_crc32c_le(uint32_t crc, const uint8_t *buffer, size_t len) {
    size_t avl = len;
    for (; avl > 0; ) {
        size_t vl = 0;

        // Example wrapper: you'll likely need to load/store vector registers.
        // Assuming v0 and v1 are used for vd and vs2 here.
        asm volatile (
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.v.x v0, %[crc] \n\t"
            "vsetvli %[vl], %[avl], e8, m1, ta, ma \n\t"
            "vle8.v v1, (%[buffer]) \n\t"
            ".insn r 0x77, 0x2, 0x51, x0, x21, x1 \n\t"
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.x.s %[crc], v0 \n\t"
            : [vl] "=r" (vl), [crc] "+r" (crc)
            : [avl] "r" (avl), [buffer] "r" (buffer)
            : "v0", "v1", "memory", "cc"
        );
        // printf("  crc=%x\n", crc);
        avl -= vl;
        buffer += vl;
    }
    return crc;
}

