#ifndef SOFTFLOAT_STUB_H
#define SOFTFLOAT_STUB_H

/*
 * Minimal softfloat flag stubs.
 * These match the Berkeley SoftFloat-3 flag definitions.
 * Replace with the real softfloat.h if available.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define softfloat_flag_invalid   0x10
#define softfloat_flag_overflow  0x04

#ifdef __cplusplus
}
#endif

#endif /* SOFTFLOAT_STUB_H */
