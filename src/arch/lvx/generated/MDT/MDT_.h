/*
 * Minimal MDT header for the LVX gem5 port.
 *
 * The MDS-generated Decode.c does #include "MDT/MDT_.h" and needs only the
 * Opcode enum. We build that enum directly from Opcode.tuple's X-macro instead
 * of vendoring Kalray's full MDT runtime.
 */
#ifndef __ARCH_LVX_GENERATED_MDT_MDT__H__
#define __ARCH_LVX_GENERATED_MDT_MDT__H__

#include <stdint.h>

// Opcode identifiers: Opcode__UNDEF plus one Opcode_lvx_v1_<...> per opcode.
typedef enum Opcode
{
    Opcode__UNDEF = 0,
#define Opcode(ID, SCHEDULING, DECODING, CODEWORDS, OPERANDS, \
               INCREMENT, MNEMONIC, SYNTAX, ACTIONARRAY) Opcode_##ID,
#include "../Opcode.tuple"
#undef Opcode
    Opcode__NUM
} Opcode;

// Register identifiers: the behavior bodies reference a handful of these
// (Register_lvx_v1_R0/P0/Q0/PC) as the base of each register file, to turn a
// decoded field into a file-relative index (e.g. decoded[i] - ..._R0). Built
// from Register.tuple's X-macro, matching MDS's own Register_ enum layout
// (leading Register_ == 0, one Register_<ID> per entry).
typedef enum Register
{
    Register_ = 0,
#define Register(ID, NAMES, REGFILES, WIRED, STORAGECELLS, MINREADSTAGES) \
    Register_##ID,
#include "../Register.tuple"
#undef Register
    Register__
} Register;

#endif // __ARCH_LVX_GENERATED_MDT_MDT__H__
