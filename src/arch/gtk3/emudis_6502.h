/** \file   emudis_6502.h
 * \brief   C64-EmuDis - 6502/6510 opcode table, assembler and disassembler
 */

/*
 * This file is part of C64-EmuDis, a modified version of VICE.
 * Copyright (C) 2026 IT-Dummkopfen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef VICE_EMUDIS_6502_H
#define VICE_EMUDIS_6502_H

#include <stdbool.h>
#include <stdint.h>

typedef enum emudis_amode_e {
    AM_IMP = 0, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY, AM_ABS, AM_ABX, AM_ABY,
    AM_IND, AM_IZX, AM_IZY, AM_REL, AM_COUNT
} emudis_amode_t;

typedef struct emudis_opcode_s {
    const char    *mnemonic;
    emudis_amode_t mode;
    bool           illegal;
} emudis_opcode_t;

extern const emudis_opcode_t emudis_opcodes[256];

int  emudis_amode_size(emudis_amode_t mode);
int  emudis_opcode_size(uint8_t opcode);

/* ---- disassembler ---- */

typedef enum emudis_syntax_e {
    EMUDIS_SYNTAX_LISTING = 0,  /**< address, bytes, instruction */
    EMUDIS_SYNTAX_ACME,         /**< ACME cross assembler source */
    EMUDIS_SYNTAX_KICKASS       /**< Kick Assembler source */
} emudis_syntax_t;

/** \brief  Disassemble one instruction to text (no labels)
 *
 * \return  instruction size in bytes
 */
int emudis_disasm_line(const uint8_t *mem, uint16_t addr, char *buf, int bufsize,
                       bool show_bytes);

/** \brief  Disassemble a memory range to assembler source
 *
 * \param[in]   mem         64KB memory image
 * \param[in]   start       first address
 * \param[in]   end         last address (inclusive)
 * \param[in]   syntax      output syntax
 * \param[in]   labels      generate labels for jump/branch targets
 * \param[in]   syslabels   use names for KERNAL, VIC, SID and CIA addresses
 * \param[in]   illegal_as_data  emit illegal opcodes as .byte
 *
 * \return  newly allocated string (free with lib_free())
 */
char *emudis_disasm_range(const uint8_t *mem, uint16_t start, uint16_t end,
                          emudis_syntax_t syntax, bool labels, bool syslabels,
                          bool illegal_as_data);

const char *emudis_syslabel(uint16_t addr);

/* ---- assembler ---- */

typedef struct emudis_asm_result_s {
    uint8_t  *mem;          /**< 64KB image with assembled bytes */
    uint8_t  *used;         /**< 64KB map: 1 = byte written */
    int       lowest;       /**< lowest written address, -1 if nothing */
    int       highest;      /**< highest written address */
    int       bytes;        /**< number of bytes written */
    int       errors;       /**< number of errors */
    char     *listing;      /**< listing + messages (lib_malloc'ed) */
    int       first_error_line; /**< 1-based line of first error, 0 if none */
} emudis_asm_result_t;

/** \brief  Assemble source text (two pass)
 *
 * \param[in]   source  assembler source
 * \param[in]   origin  default start address (if source has no *=)
 * \param[out]  result  result, free with emudis_asm_result_free()
 *
 * \return  true when no errors occurred
 */
bool emudis_assemble(const char *source, uint16_t origin, emudis_asm_result_t *result);
void emudis_asm_result_free(emudis_asm_result_t *result);

#endif
