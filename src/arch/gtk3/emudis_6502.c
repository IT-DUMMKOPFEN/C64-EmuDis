/** \file   emudis_6502.c
 * \brief   C64-EmuDis - 6502/6510 opcode table, assembler and disassembler
 *
 * The assembler is a small two-pass cross assembler that understands the
 * most common ACME / Kick Assembler style syntax:
 *
 *  - labels (`label`, `label:`), constants (`name = expr`)
 *  - `*=expr`, `.org expr`, `!org expr`, `.pc expr`
 *  - `.byte/.by/!byte/!by/.db`, `.word/.wo/!word/!wo/.dw`
 *  - `.text/!text/.ascii` (raw), `!pet/.petscii` (PETSCII), `!scr/.screen`
 *  - `.fill/!fill count[,value]`, `.res count`
 *  - expressions: + - * / % & | ^ << >> < (low) > (high) ( )
 *  - numbers: $hex, 0xhex, %binary, decimal, 'c'; `*` is the current PC
 *  - all official opcodes plus the common undocumented ones
 *
 * The disassembler produces listings or re-assemblable ACME / Kick Assembler
 * source with automatic labels, inspired by Dis64 by Shallan (Steve Day).
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

#include "vice.h"

#include <ctype.h>
#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"

#include "emudis_version.h"
#include "emudis_6502.h"

#define EMUDIS_MEM_SIZE_6502 0x10000


/* ------------------------------------------------------------------------ */
/* Opcode table                                                             */
/* ------------------------------------------------------------------------ */

#define L(m, md) { m, md, false }
#define X(m, md) { m, md, true }

const emudis_opcode_t emudis_opcodes[256] = {
    /* 0x00 */
    L("BRK", AM_IMP), L("ORA", AM_IZX), X("JAM", AM_IMP), X("SLO", AM_IZX),
    X("NOP", AM_ZP),  L("ORA", AM_ZP),  L("ASL", AM_ZP),  X("SLO", AM_ZP),
    L("PHP", AM_IMP), L("ORA", AM_IMM), L("ASL", AM_ACC), X("ANC", AM_IMM),
    X("NOP", AM_ABS), L("ORA", AM_ABS), L("ASL", AM_ABS), X("SLO", AM_ABS),
    /* 0x10 */
    L("BPL", AM_REL), L("ORA", AM_IZY), X("JAM", AM_IMP), X("SLO", AM_IZY),
    X("NOP", AM_ZPX), L("ORA", AM_ZPX), L("ASL", AM_ZPX), X("SLO", AM_ZPX),
    L("CLC", AM_IMP), L("ORA", AM_ABY), X("NOP", AM_IMP), X("SLO", AM_ABY),
    X("NOP", AM_ABX), L("ORA", AM_ABX), L("ASL", AM_ABX), X("SLO", AM_ABX),
    /* 0x20 */
    L("JSR", AM_ABS), L("AND", AM_IZX), X("JAM", AM_IMP), X("RLA", AM_IZX),
    L("BIT", AM_ZP),  L("AND", AM_ZP),  L("ROL", AM_ZP),  X("RLA", AM_ZP),
    L("PLP", AM_IMP), L("AND", AM_IMM), L("ROL", AM_ACC), X("ANC", AM_IMM),
    L("BIT", AM_ABS), L("AND", AM_ABS), L("ROL", AM_ABS), X("RLA", AM_ABS),
    /* 0x30 */
    L("BMI", AM_REL), L("AND", AM_IZY), X("JAM", AM_IMP), X("RLA", AM_IZY),
    X("NOP", AM_ZPX), L("AND", AM_ZPX), L("ROL", AM_ZPX), X("RLA", AM_ZPX),
    L("SEC", AM_IMP), L("AND", AM_ABY), X("NOP", AM_IMP), X("RLA", AM_ABY),
    X("NOP", AM_ABX), L("AND", AM_ABX), L("ROL", AM_ABX), X("RLA", AM_ABX),
    /* 0x40 */
    L("RTI", AM_IMP), L("EOR", AM_IZX), X("JAM", AM_IMP), X("SRE", AM_IZX),
    X("NOP", AM_ZP),  L("EOR", AM_ZP),  L("LSR", AM_ZP),  X("SRE", AM_ZP),
    L("PHA", AM_IMP), L("EOR", AM_IMM), L("LSR", AM_ACC), X("ALR", AM_IMM),
    L("JMP", AM_ABS), L("EOR", AM_ABS), L("LSR", AM_ABS), X("SRE", AM_ABS),
    /* 0x50 */
    L("BVC", AM_REL), L("EOR", AM_IZY), X("JAM", AM_IMP), X("SRE", AM_IZY),
    X("NOP", AM_ZPX), L("EOR", AM_ZPX), L("LSR", AM_ZPX), X("SRE", AM_ZPX),
    L("CLI", AM_IMP), L("EOR", AM_ABY), X("NOP", AM_IMP), X("SRE", AM_ABY),
    X("NOP", AM_ABX), L("EOR", AM_ABX), L("LSR", AM_ABX), X("SRE", AM_ABX),
    /* 0x60 */
    L("RTS", AM_IMP), L("ADC", AM_IZX), X("JAM", AM_IMP), X("RRA", AM_IZX),
    X("NOP", AM_ZP),  L("ADC", AM_ZP),  L("ROR", AM_ZP),  X("RRA", AM_ZP),
    L("PLA", AM_IMP), L("ADC", AM_IMM), L("ROR", AM_ACC), X("ARR", AM_IMM),
    L("JMP", AM_IND), L("ADC", AM_ABS), L("ROR", AM_ABS), X("RRA", AM_ABS),
    /* 0x70 */
    L("BVS", AM_REL), L("ADC", AM_IZY), X("JAM", AM_IMP), X("RRA", AM_IZY),
    X("NOP", AM_ZPX), L("ADC", AM_ZPX), L("ROR", AM_ZPX), X("RRA", AM_ZPX),
    L("SEI", AM_IMP), L("ADC", AM_ABY), X("NOP", AM_IMP), X("RRA", AM_ABY),
    X("NOP", AM_ABX), L("ADC", AM_ABX), L("ROR", AM_ABX), X("RRA", AM_ABX),
    /* 0x80 */
    X("NOP", AM_IMM), L("STA", AM_IZX), X("NOP", AM_IMM), X("SAX", AM_IZX),
    L("STY", AM_ZP),  L("STA", AM_ZP),  L("STX", AM_ZP),  X("SAX", AM_ZP),
    L("DEY", AM_IMP), X("NOP", AM_IMM), L("TXA", AM_IMP), X("ANE", AM_IMM),
    L("STY", AM_ABS), L("STA", AM_ABS), L("STX", AM_ABS), X("SAX", AM_ABS),
    /* 0x90 */
    L("BCC", AM_REL), L("STA", AM_IZY), X("JAM", AM_IMP), X("SHA", AM_IZY),
    L("STY", AM_ZPX), L("STA", AM_ZPX), L("STX", AM_ZPY), X("SAX", AM_ZPY),
    L("TYA", AM_IMP), L("STA", AM_ABY), L("TXS", AM_IMP), X("TAS", AM_ABY),
    X("SHY", AM_ABX), L("STA", AM_ABX), X("SHX", AM_ABY), X("SHA", AM_ABY),
    /* 0xA0 */
    L("LDY", AM_IMM), L("LDA", AM_IZX), L("LDX", AM_IMM), X("LAX", AM_IZX),
    L("LDY", AM_ZP),  L("LDA", AM_ZP),  L("LDX", AM_ZP),  X("LAX", AM_ZP),
    L("TAY", AM_IMP), L("LDA", AM_IMM), L("TAX", AM_IMP), X("LXA", AM_IMM),
    L("LDY", AM_ABS), L("LDA", AM_ABS), L("LDX", AM_ABS), X("LAX", AM_ABS),
    /* 0xB0 */
    L("BCS", AM_REL), L("LDA", AM_IZY), X("JAM", AM_IMP), X("LAX", AM_IZY),
    L("LDY", AM_ZPX), L("LDA", AM_ZPX), L("LDX", AM_ZPY), X("LAX", AM_ZPY),
    L("CLV", AM_IMP), L("LDA", AM_ABY), L("TSX", AM_IMP), X("LAS", AM_ABY),
    L("LDY", AM_ABX), L("LDA", AM_ABX), L("LDX", AM_ABY), X("LAX", AM_ABY),
    /* 0xC0 */
    L("CPY", AM_IMM), L("CMP", AM_IZX), X("NOP", AM_IMM), X("DCP", AM_IZX),
    L("CPY", AM_ZP),  L("CMP", AM_ZP),  L("DEC", AM_ZP),  X("DCP", AM_ZP),
    L("INY", AM_IMP), L("CMP", AM_IMM), L("DEX", AM_IMP), X("SBX", AM_IMM),
    L("CPY", AM_ABS), L("CMP", AM_ABS), L("DEC", AM_ABS), X("DCP", AM_ABS),
    /* 0xD0 */
    L("BNE", AM_REL), L("CMP", AM_IZY), X("JAM", AM_IMP), X("DCP", AM_IZY),
    X("NOP", AM_ZPX), L("CMP", AM_ZPX), L("DEC", AM_ZPX), X("DCP", AM_ZPX),
    L("CLD", AM_IMP), L("CMP", AM_ABY), X("NOP", AM_IMP), X("DCP", AM_ABY),
    X("NOP", AM_ABX), L("CMP", AM_ABX), L("DEC", AM_ABX), X("DCP", AM_ABX),
    /* 0xE0 */
    L("CPX", AM_IMM), L("SBC", AM_IZX), X("NOP", AM_IMM), X("ISC", AM_IZX),
    L("CPX", AM_ZP),  L("SBC", AM_ZP),  L("INC", AM_ZP),  X("ISC", AM_ZP),
    L("INX", AM_IMP), L("SBC", AM_IMM), L("NOP", AM_IMP), X("SBC", AM_IMM),
    L("CPX", AM_ABS), L("SBC", AM_ABS), L("INC", AM_ABS), X("ISC", AM_ABS),
    /* 0xF0 */
    L("BEQ", AM_REL), L("SBC", AM_IZY), X("JAM", AM_IMP), X("ISC", AM_IZY),
    X("NOP", AM_ZPX), L("SBC", AM_ZPX), L("INC", AM_ZPX), X("ISC", AM_ZPX),
    L("SED", AM_IMP), L("SBC", AM_ABY), X("NOP", AM_IMP), X("ISC", AM_ABY),
    X("NOP", AM_ABX), L("SBC", AM_ABX), L("INC", AM_ABX), X("ISC", AM_ABX)
};

#undef L
#undef X

static const int amode_sizes[AM_COUNT] = {
    1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2
};

int emudis_amode_size(emudis_amode_t mode)
{
    return amode_sizes[mode];
}

int emudis_opcode_size(uint8_t opcode)
{
    return amode_sizes[emudis_opcodes[opcode].mode];
}


/* ------------------------------------------------------------------------ */
/* System labels                                                            */
/* ------------------------------------------------------------------------ */

typedef struct syslabel_s {
    uint16_t    addr;
    const char *name;
} syslabel_t;

static const syslabel_t syslabels[] = {
    { 0x0000, "CPU_DDR" }, { 0x0001, "CPU_PORT" },
    { 0x0314, "IRQ_VECTOR" }, { 0x0316, "BRK_VECTOR" }, { 0x0318, "NMI_VECTOR" },
    /* VIC-II */
    { 0xd000, "SPRITE0_X" }, { 0xd001, "SPRITE0_Y" }, { 0xd002, "SPRITE1_X" },
    { 0xd003, "SPRITE1_Y" }, { 0xd004, "SPRITE2_X" }, { 0xd005, "SPRITE2_Y" },
    { 0xd006, "SPRITE3_X" }, { 0xd007, "SPRITE3_Y" }, { 0xd008, "SPRITE4_X" },
    { 0xd009, "SPRITE4_Y" }, { 0xd00a, "SPRITE5_X" }, { 0xd00b, "SPRITE5_Y" },
    { 0xd00c, "SPRITE6_X" }, { 0xd00d, "SPRITE6_Y" }, { 0xd00e, "SPRITE7_X" },
    { 0xd00f, "SPRITE7_Y" }, { 0xd010, "SPRITES_X_MSB" }, { 0xd011, "VIC_CTRL1" },
    { 0xd012, "VIC_RASTER" }, { 0xd013, "VIC_LIGHTPEN_X" }, { 0xd014, "VIC_LIGHTPEN_Y" },
    { 0xd015, "SPRITE_ENABLE" }, { 0xd016, "VIC_CTRL2" }, { 0xd017, "SPRITE_EXPAND_Y" },
    { 0xd018, "VIC_MEMSETUP" }, { 0xd019, "VIC_IRQ_STATUS" }, { 0xd01a, "VIC_IRQ_ENABLE" },
    { 0xd01b, "SPRITE_PRIORITY" }, { 0xd01c, "SPRITE_MULTICOLOR" },
    { 0xd01d, "SPRITE_EXPAND_X" }, { 0xd01e, "SPRITE_COLL_SPRITE" },
    { 0xd01f, "SPRITE_COLL_BG" }, { 0xd020, "BORDER_COLOR" }, { 0xd021, "BG_COLOR0" },
    { 0xd022, "BG_COLOR1" }, { 0xd023, "BG_COLOR2" }, { 0xd024, "BG_COLOR3" },
    { 0xd025, "SPRITE_MC0" }, { 0xd026, "SPRITE_MC1" }, { 0xd027, "SPRITE0_COLOR" },
    { 0xd028, "SPRITE1_COLOR" }, { 0xd029, "SPRITE2_COLOR" }, { 0xd02a, "SPRITE3_COLOR" },
    { 0xd02b, "SPRITE4_COLOR" }, { 0xd02c, "SPRITE5_COLOR" }, { 0xd02d, "SPRITE6_COLOR" },
    { 0xd02e, "SPRITE7_COLOR" },
    /* SID */
    { 0xd400, "SID_V1_FREQ_LO" }, { 0xd401, "SID_V1_FREQ_HI" }, { 0xd402, "SID_V1_PW_LO" },
    { 0xd403, "SID_V1_PW_HI" }, { 0xd404, "SID_V1_CTRL" }, { 0xd405, "SID_V1_AD" },
    { 0xd406, "SID_V1_SR" }, { 0xd407, "SID_V2_FREQ_LO" }, { 0xd408, "SID_V2_FREQ_HI" },
    { 0xd409, "SID_V2_PW_LO" }, { 0xd40a, "SID_V2_PW_HI" }, { 0xd40b, "SID_V2_CTRL" },
    { 0xd40c, "SID_V2_AD" }, { 0xd40d, "SID_V2_SR" }, { 0xd40e, "SID_V3_FREQ_LO" },
    { 0xd40f, "SID_V3_FREQ_HI" }, { 0xd410, "SID_V3_PW_LO" }, { 0xd411, "SID_V3_PW_HI" },
    { 0xd412, "SID_V3_CTRL" }, { 0xd413, "SID_V3_AD" }, { 0xd414, "SID_V3_SR" },
    { 0xd415, "SID_FILTER_LO" }, { 0xd416, "SID_FILTER_HI" }, { 0xd417, "SID_RES_FILT" },
    { 0xd418, "SID_MODE_VOL" }, { 0xd419, "SID_POT_X" }, { 0xd41a, "SID_POT_Y" },
    { 0xd41b, "SID_OSC3" }, { 0xd41c, "SID_ENV3" },
    /* CIA 1 */
    { 0xdc00, "CIA1_PRA" }, { 0xdc01, "CIA1_PRB" }, { 0xdc02, "CIA1_DDRA" },
    { 0xdc03, "CIA1_DDRB" }, { 0xdc04, "CIA1_TA_LO" }, { 0xdc05, "CIA1_TA_HI" },
    { 0xdc06, "CIA1_TB_LO" }, { 0xdc07, "CIA1_TB_HI" }, { 0xdc08, "CIA1_TOD_10THS" },
    { 0xdc09, "CIA1_TOD_SEC" }, { 0xdc0a, "CIA1_TOD_MIN" }, { 0xdc0b, "CIA1_TOD_HR" },
    { 0xdc0c, "CIA1_SDR" }, { 0xdc0d, "CIA1_ICR" }, { 0xdc0e, "CIA1_CRA" },
    { 0xdc0f, "CIA1_CRB" },
    /* CIA 2 */
    { 0xdd00, "CIA2_PRA" }, { 0xdd01, "CIA2_PRB" }, { 0xdd02, "CIA2_DDRA" },
    { 0xdd03, "CIA2_DDRB" }, { 0xdd04, "CIA2_TA_LO" }, { 0xdd05, "CIA2_TA_HI" },
    { 0xdd06, "CIA2_TB_LO" }, { 0xdd07, "CIA2_TB_HI" }, { 0xdd08, "CIA2_TOD_10THS" },
    { 0xdd09, "CIA2_TOD_SEC" }, { 0xdd0a, "CIA2_TOD_MIN" }, { 0xdd0b, "CIA2_TOD_HR" },
    { 0xdd0c, "CIA2_SDR" }, { 0xdd0d, "CIA2_ICR" }, { 0xdd0e, "CIA2_CRA" },
    { 0xdd0f, "CIA2_CRB" },
    /* KERNAL */
    { 0xea31, "KERNAL_IRQ" }, { 0xea81, "KERNAL_IRQ_EXIT" },
    { 0xff81, "CINT" }, { 0xff84, "IOINIT" }, { 0xff87, "RAMTAS" }, { 0xff8a, "RESTOR" },
    { 0xff8d, "VECTOR" }, { 0xff90, "SETMSG" }, { 0xff93, "SECOND" }, { 0xff96, "TKSA" },
    { 0xff99, "MEMTOP" }, { 0xff9c, "MEMBOT" }, { 0xff9f, "SCNKEY" }, { 0xffa2, "SETTMO" },
    { 0xffa5, "ACPTR" }, { 0xffa8, "CIOUT" }, { 0xffab, "UNTLK" }, { 0xffae, "UNLSN" },
    { 0xffb1, "LISTEN" }, { 0xffb4, "TALK" }, { 0xffb7, "READST" }, { 0xffba, "SETLFS" },
    { 0xffbd, "SETNAM" }, { 0xffc0, "OPEN" }, { 0xffc3, "CLOSE" }, { 0xffc6, "CHKIN" },
    { 0xffc9, "CHKOUT" }, { 0xffcc, "CLRCHN" }, { 0xffcf, "CHRIN" }, { 0xffd2, "CHROUT" },
    { 0xffd5, "LOAD" }, { 0xffd8, "SAVE" }, { 0xffdb, "SETTIM" }, { 0xffde, "RDTIM" },
    { 0xffe1, "STOP" }, { 0xffe4, "GETIN" }, { 0xffe7, "CLALL" }, { 0xffea, "UDTIM" },
    { 0xffed, "SCREEN" }, { 0xfff0, "PLOT" }, { 0xfff3, "IOBASE" },
    { 0xfffa, "NMI_VEC" }, { 0xfffc, "RESET_VEC" }, { 0xfffe, "IRQ_VEC" },
    { 0, NULL }
};

const char *emudis_syslabel(uint16_t addr)
{
    int i;

    for (i = 0; syslabels[i].name != NULL; i++) {
        if (syslabels[i].addr == addr) {
            return syslabels[i].name;
        }
    }
    return NULL;
}


/* ------------------------------------------------------------------------ */
/* Disassembler                                                             */
/* ------------------------------------------------------------------------ */

/* Format the operand of an instruction. `target` is the resolved address
 * for branches. `name` replaces the numeric address when non-NULL. */
static void format_operand(char *buf, int size, emudis_amode_t mode,
                           unsigned int value, const char *name, bool lower)
{
    char num[64];

    if (name != NULL) {
        g_strlcpy(num, name, sizeof num);
    } else if (mode == AM_IMM || mode == AM_ZP || mode == AM_ZPX
               || mode == AM_ZPY || mode == AM_IZX || mode == AM_IZY) {
        g_snprintf(num, sizeof num, lower ? "$%02x" : "$%02X", value & 0xff);
    } else {
        g_snprintf(num, sizeof num, lower ? "$%04x" : "$%04X", value & 0xffff);
    }

    switch (mode) {
        case AM_IMP: buf[0] = '\0'; break;
        case AM_ACC: buf[0] = '\0'; break;
        case AM_IMM: g_snprintf(buf, size, "#%s", num); break;
        case AM_ZP:
        case AM_ABS:
        case AM_REL: g_snprintf(buf, size, "%s", num); break;
        case AM_ZPX:
        case AM_ABX: g_snprintf(buf, size, lower ? "%s,x" : "%s,X", num); break;
        case AM_ZPY:
        case AM_ABY: g_snprintf(buf, size, lower ? "%s,y" : "%s,Y", num); break;
        case AM_IND: g_snprintf(buf, size, "(%s)", num); break;
        case AM_IZX: g_snprintf(buf, size, lower ? "(%s,x)" : "(%s,X)", num); break;
        case AM_IZY: g_snprintf(buf, size, lower ? "(%s),y" : "(%s),Y", num); break;
        default: buf[0] = '\0'; break;
    }
}

static unsigned int operand_value(const uint8_t *mem, uint16_t addr, emudis_amode_t mode)
{
    uint8_t lo = mem[(uint16_t)(addr + 1)];
    uint8_t hi = mem[(uint16_t)(addr + 2)];

    switch (amode_sizes[mode]) {
        case 2:
            if (mode == AM_REL) {
                return (uint16_t)(addr + 2 + (int8_t)lo);
            }
            return lo;
        case 3:
            return (unsigned int)(lo | (hi << 8));
        default:
            return 0;
    }
}

int emudis_disasm_line(const uint8_t *mem, uint16_t addr, char *buf, int bufsize,
                       bool show_bytes)
{
    uint8_t op = mem[addr];
    const emudis_opcode_t *oc = &emudis_opcodes[op];
    int size = amode_sizes[oc->mode];
    char operand[64];
    char bytes[16] = "";
    int i;

    format_operand(operand, sizeof operand, oc->mode,
                   operand_value(mem, addr, oc->mode), NULL, false);
    if (show_bytes) {
        char *p = bytes;
        for (i = 0; i < 3; i++) {
            if (i < size) {
                g_snprintf(p, 4, "%02X ", mem[(uint16_t)(addr + i)]);
            } else {
                g_strlcpy(p, "   ", 4);
            }
            p += 3;
        }
        g_snprintf(buf, bufsize, "%04X  %s %s%s%s%s", (unsigned int)addr, bytes,
                   oc->illegal ? "*" : " ", oc->mnemonic,
                   operand[0] ? " " : "", operand);
    } else {
        g_snprintf(buf, bufsize, "%s%s%s", oc->mnemonic, operand[0] ? " " : "", operand);
    }
    return size;
}

/* label kinds */
#define LBL_NONE 0
#define LBL_CODE 1
#define LBL_SUB  2
#define LBL_DATA 3

static bool is_flow_target_insn(const emudis_opcode_t *oc)
{
    return oc->mode == AM_REL
           || (oc->mode == AM_ABS && (strcmp(oc->mnemonic, "JMP") == 0
                                      || strcmp(oc->mnemonic, "JSR") == 0));
}

char *emudis_disasm_range(const uint8_t *mem, uint16_t start, uint16_t end,
                          emudis_syntax_t syntax, bool labels, bool syslabels_on,
                          bool illegal_as_data)
{
    GString *out = g_string_new(NULL);
    uint8_t *kind = lib_calloc(EMUDIS_MEM_SIZE_6502, 1);
    uint8_t *insn = lib_calloc(EMUDIS_MEM_SIZE_6502, 1);  /* 1 = instruction start, 2 = data */
    bool *sys_used = NULL;
    int nsys = 0;
    unsigned int a;
    unsigned int last = end;
    const char *cmt = (syntax == EMUDIS_SYNTAX_KICKASS) ? "//" : ";";
    bool src = syntax != EMUDIS_SYNTAX_LISTING;
    char *result;
    int i;

    while (syslabels[nsys].name != NULL) {
        nsys++;
    }
    sys_used = lib_calloc((size_t)nsys + 1, sizeof(bool));

    if (last < start) {
        last = start;
    }

    /* pass 1: decode and collect targets */
    a = start;
    while (a <= last) {
        uint8_t op = mem[a];
        const emudis_opcode_t *oc = &emudis_opcodes[op];
        int size = amode_sizes[oc->mode];

        if ((oc->illegal && illegal_as_data) || a + (unsigned int)size - 1 > last) {
            insn[a] = 2;
            a++;
            continue;
        }
        insn[a] = 1;
        if (labels && size > 1 && oc->mode != AM_IMM) {
            unsigned int v = operand_value(mem, (uint16_t)a, oc->mode);
            if (v >= start && v <= last && oc->mode != AM_ZP && oc->mode != AM_ZPX
                && oc->mode != AM_ZPY && oc->mode != AM_IZX && oc->mode != AM_IZY) {
                if (strcmp(oc->mnemonic, "JSR") == 0) {
                    kind[v] = LBL_SUB;
                } else if (is_flow_target_insn(oc) || oc->mode == AM_IND) {
                    if (kind[v] != LBL_SUB) {
                        kind[v] = LBL_CODE;
                    }
                } else if (kind[v] == LBL_NONE) {
                    kind[v] = LBL_DATA;
                }
            }
        }
        a += (unsigned int)size;
    }

    /* drop labels that don't point to the start of an instruction/data byte */
    for (a = start; a <= last; a++) {
        if (kind[a] != LBL_NONE && insn[a] == 0) {
            kind[a] = LBL_NONE;
        }
    }

    /* find used system labels */
    if (syslabels_on && src) {
        for (a = start; a <= last; a++) {
            if (insn[a] == 1) {
                const emudis_opcode_t *oc = &emudis_opcodes[mem[a]];
                int size = amode_sizes[oc->mode];
                if (size == 3 || (size == 2 && oc->mode != AM_IMM && oc->mode != AM_REL)) {
                    unsigned int v = operand_value(mem, (uint16_t)a, oc->mode);
                    for (i = 0; i < nsys; i++) {
                        if (syslabels[i].addr == v && (size == 3 || v < 0x100)) {
                            sys_used[i] = true;
                        }
                    }
                }
            }
        }
    }

    /* header */
    if (src) {
        g_string_append_printf(out, "%s %s - disassembled by %s\n", cmt,
                               syntax == EMUDIS_SYNTAX_ACME ? "ACME source" : "Kick Assembler source",
                               EMUDIS_TITLE);
        g_string_append_printf(out, "%s Range $%04X-$%04X\n\n", cmt, start, last);
        if (syntax == EMUDIS_SYNTAX_ACME) {
            g_string_append(out, "!cpu 6510\n\n");
        }
        if (syslabels_on) {
            bool any = false;
            for (i = 0; i < nsys; i++) {
                if (sys_used[i]) {
                    if (syntax == EMUDIS_SYNTAX_KICKASS) {
                        g_string_append_printf(out, ".label %-20s = $%04x\n",
                                               syslabels[i].name, syslabels[i].addr);
                    } else {
                        g_string_append_printf(out, "%-20s = $%04x\n",
                                               syslabels[i].name, syslabels[i].addr);
                    }
                    any = true;
                }
            }
            if (any) {
                g_string_append(out, "\n");
            }
        }
        if (syntax == EMUDIS_SYNTAX_KICKASS) {
            g_string_append_printf(out, "* = $%04x\n\n", start);
        } else {
            g_string_append_printf(out, "        * = $%04x\n\n", start);
        }
    }

    /* pass 2: output */
    a = start;
    while (a <= last) {
        char lbl[32] = "";
        char line[160];

        if (kind[a] != LBL_NONE) {
            g_snprintf(lbl, sizeof lbl, "%s%04x",
                       kind[a] == LBL_SUB ? "sub_" : kind[a] == LBL_DATA ? "dat_" : "l_", a);
        }

        if (insn[a] == 2) {
            /* run of data bytes (stop at labels / instructions) */
            unsigned int n = 0;
            GString *bytes = g_string_new(NULL);
            while (a + n <= last && insn[a + n] == 2 && n < 8
                   && (n == 0 || kind[a + n] == LBL_NONE)) {
                g_string_append_printf(bytes, "%s$%02x", n ? "," : "", mem[a + n]);
                n++;
            }
            if (!src) {
                g_string_append_printf(out, "%04X  %-24s .byte %s\n", a, lbl, bytes->str);
            } else if (syntax == EMUDIS_SYNTAX_KICKASS) {
                if (lbl[0]) {
                    g_string_append_printf(out, "%s:\n", lbl);
                }
                g_string_append_printf(out, "        .byte %s\n", bytes->str);
            } else {
                g_string_append_printf(out, "%-8s!byte %s\n", lbl, bytes->str);
            }
            g_string_free(bytes, TRUE);
            a += n;
            continue;
        }

        {
            uint8_t op = mem[a];
            const emudis_opcode_t *oc = &emudis_opcodes[op];
            int size = amode_sizes[oc->mode];
            unsigned int v = operand_value(mem, (uint16_t)a, oc->mode);
            const char *name = NULL;
            char namebuf[32];
            char operand[64];
            char mnem[8];
            bool as_bytes = false;

            g_strlcpy(mnem, oc->mnemonic, sizeof mnem);
            if (src) {
                for (i = 0; mnem[i]; i++) {
                    mnem[i] = (char)tolower((unsigned char)mnem[i]);
                }
            }

            if (size > 1 && oc->mode != AM_IMM) {
                bool zp_mode = (oc->mode == AM_ZP || oc->mode == AM_ZPX || oc->mode == AM_ZPY
                                || oc->mode == AM_IZX || oc->mode == AM_IZY);
                if (!zp_mode && v >= start && v <= last && kind[v] != LBL_NONE) {
                    g_snprintf(namebuf, sizeof namebuf, "%s%04x",
                               kind[v] == LBL_SUB ? "sub_" : kind[v] == LBL_DATA ? "dat_" : "l_", v);
                    name = namebuf;
                } else if (syslabels_on && src) {
                    const char *s = emudis_syslabel((uint16_t)v);
                    if (s != NULL && (!zp_mode || v < 0x100)) {
                        name = s;
                    }
                }
            }
            format_operand(operand, sizeof operand, oc->mode, v, name, src);

            /* things a cross assembler would encode differently */
            if (src) {
                if (oc->illegal) {
                    as_bytes = true;
                } else if (size == 3 && v < 0x100 && name == NULL
                           && oc->mode != AM_IND && strcmp(oc->mnemonic, "JMP") != 0
                           && strcmp(oc->mnemonic, "JSR") != 0) {
                    as_bytes = true;    /* absolute addressing of a zero page address */
                } else if (size == 3 && v < 0x100 && name != NULL
                           && oc->mode != AM_IND && strcmp(oc->mnemonic, "JMP") != 0
                           && strcmp(oc->mnemonic, "JSR") != 0) {
                    as_bytes = true;
                }
            }

            if (!src) {
                char bytes[16] = "";
                char *p = bytes;
                for (i = 0; i < 3; i++) {
                    if (i < size) {
                        g_snprintf(p, 4, "%02X ", mem[(a + (unsigned int)i) & 0xffff]);
                    } else {
                        g_strlcpy(p, "   ", 4);
                    }
                    p += 3;
                }
                g_snprintf(line, sizeof line, "%04X  %s %-12s %s%s%s%s", a, bytes, lbl,
                           oc->illegal ? "*" : " ", mnem, operand[0] ? " " : "", operand);
                g_string_append_printf(out, "%s\n", line);
            } else {
                GString *ins = g_string_new(NULL);
                if (as_bytes) {
                    g_string_append(ins, syntax == EMUDIS_SYNTAX_KICKASS ? ".byte " : "!byte ");
                    for (i = 0; i < size; i++) {
                        g_string_append_printf(ins, "%s$%02x", i ? "," : "",
                                               mem[(a + (unsigned int)i) & 0xffff]);
                    }
                    g_string_append_printf(ins, "   %s %s%s%s", cmt, mnem,
                                           operand[0] ? " " : "", operand);
                } else {
                    g_string_append_printf(ins, "%s%s%s", mnem, operand[0] ? " " : "", operand);
                }
                if (syntax == EMUDIS_SYNTAX_KICKASS) {
                    if (lbl[0]) {
                        g_string_append_printf(out, "%s:\n", lbl);
                    }
                    g_string_append_printf(out, "        %s\n", ins->str);
                } else {
                    if (strlen(lbl) >= 8) {
                        g_string_append_printf(out, "%s\n        %s\n", lbl, ins->str);
                    } else {
                        g_string_append_printf(out, "%-8s%s\n", lbl, ins->str);
                    }
                }
                /* blank line after flow breaks for readability */
                if (op == 0x60 || op == 0x40 || op == 0x4c || op == 0x6c) {
                    g_string_append(out, "\n");
                }
                g_string_free(ins, TRUE);
            }
            a += (unsigned int)size;
        }
    }

    result = lib_strdup(out->str);
    g_string_free(out, TRUE);
    lib_free(kind);
    lib_free(insn);
    lib_free(sys_used);
    return result;
}


/* ------------------------------------------------------------------------ */
/* Assembler                                                                */
/* ------------------------------------------------------------------------ */

#define MAX_SYMBOLS 8192
#define SYM_LEN     64

typedef struct asm_symbol_s {
    char  name[SYM_LEN];
    long  value;
    int   pass_defined;
} asm_symbol_t;

typedef struct asm_state_s {
    asm_symbol_t *syms;
    int           nsyms;
    int           pass;
    long          pc;
    int           line_no;
    GString      *msg;
    GString      *list;
    int           errors;
    int           first_error_line;
    emudis_asm_result_t *res;
    uint8_t      *force_abs;    /* per line: 1 = use absolute addressing */
    int           nlines;
    /* ACME style anonymous labels ("+", "++", "-", "--") */
    struct anon_s {
        char sign;
        int  count;
        long addr;
        int  line;
    } *anon, *anon_prev;
    int           nanon, nanon_prev;
} asm_state_t;

typedef struct asm_value_s {
    long value;
    bool known;
} asm_value_t;

static void asm_error(asm_state_t *st, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void asm_error(asm_state_t *st, const char *fmt, ...)
{
    va_list ap;
    char *text;

    if (st->pass != 2) {
        return;
    }
    va_start(ap, fmt);
    text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_string_append_printf(st->msg, "Error in line %d: %s\n", st->line_no, text);
    g_string_append_printf(st->list, "*** ERROR: %s\n", text);
    g_free(text);
    if (st->errors == 0) {
        st->first_error_line = st->line_no;
    }
    st->errors++;
}

static asm_symbol_t *sym_find(asm_state_t *st, const char *name)
{
    int i;

    for (i = 0; i < st->nsyms; i++) {
        if (g_ascii_strcasecmp(st->syms[i].name, name) == 0) {
            return &st->syms[i];
        }
    }
    return NULL;
}

static void sym_define(asm_state_t *st, const char *name, long value)
{
    asm_symbol_t *s = sym_find(st, name);

    if (s == NULL) {
        if (st->nsyms >= MAX_SYMBOLS) {
            asm_error(st, "too many symbols");
            return;
        }
        s = &st->syms[st->nsyms++];
        g_strlcpy(s->name, name, sizeof s->name);
        s->value = value;
        s->pass_defined = st->pass;
        return;
    }
    if (s->pass_defined == st->pass) {
        if (st->pass == 1) {
            /* report in pass 2 */
            s->pass_defined = -1;
        } else {
            asm_error(st, "symbol '%s' defined more than once", name);
        }
        return;
    }
    if (s->pass_defined == -1 && st->pass == 2) {
        asm_error(st, "symbol '%s' defined more than once", name);
        s->pass_defined = 2;
        return;
    }
    if (st->pass == 2 && s->value != value) {
        asm_error(st, "symbol '%s' changed value between passes ($%04lX -> $%04lX)",
                  name, (unsigned long)s->value, (unsigned long)value);
    }
    s->value = value;
    s->pass_defined = st->pass;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static bool is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_' || c == '.' || c == '@';
}

static bool is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '@';
}

static const char *read_ident(const char *p, char *buf, int size)
{
    int n = 0;

    while (is_ident_char(*p)) {
        if (n < size - 1) {
            buf[n++] = *p;
        }
        p++;
    }
    buf[n] = '\0';
    return p;
}

/* forward */
static asm_value_t parse_expr(asm_state_t *st, const char **pp, bool *ok);

static asm_value_t parse_primary(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t v = { 0, true };
    const char *p = skip_ws(*pp);

    if (*p == '(') {
        p++;
        v = parse_expr(st, &p, ok);
        p = skip_ws(p);
        if (*p != ')') {
            asm_error(st, "missing ')'");
            *ok = false;
        } else {
            p++;
        }
    } else if (*p == '$') {
        char *end;
        p++;
        if (!isxdigit((unsigned char)*p)) {
            asm_error(st, "bad hex number");
            *ok = false;
        }
        v.value = strtol(p, &end, 16);
        p = end;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        char *end;
        v.value = strtol(p + 2, &end, 16);
        p = end;
    } else if (*p == '%') {
        long val = 0;
        p++;
        if (*p != '0' && *p != '1' && *p != '.' && *p != '#') {
            asm_error(st, "bad binary number");
            *ok = false;
        }
        while (*p == '0' || *p == '1' || *p == '.' || *p == '#') {
            val = (val << 1) | ((*p == '1' || *p == '#') ? 1 : 0);
            p++;
        }
        v.value = val;
    } else if (isdigit((unsigned char)*p)) {
        char *end;
        v.value = strtol(p, &end, 10);
        p = end;
    } else if (*p == '\'' && p[1] != '\0' && p[2] == '\'') {
        v.value = (unsigned char)p[1];
        p += 3;
    } else if (*p == '*') {
        v.value = st->pc;
        p++;
    } else if (is_ident_start(*p)) {
        char name[SYM_LEN];
        asm_symbol_t *s;
        p = read_ident(p, name, sizeof name);
        s = sym_find(st, name);
        if (s == NULL) {
            v.known = false;
            v.value = 0;
            if (st->pass == 2) {
                asm_error(st, "undefined symbol '%s'", name);
                *ok = false;
            }
        } else {
            v.value = s->value;
            /* in pass 1 a symbol defined later in the source is not known yet */
            v.known = true;
        }
    } else {
        asm_error(st, "syntax error in expression near '%s'", p);
        *ok = false;
        if (*p) {
            p++;
        }
    }
    *pp = p;
    return v;
}

#define MAX_ANON 4096

/* is p an anonymous label reference like "+", "--" (followed by end/,/)) ? */
static int anon_ref_len(const char *p)
{
    int n = 0;
    char c = *p;

    if (c != '+' && c != '-') {
        return 0;
    }
    while (p[n] == c) {
        n++;
    }
    {
        const char *q = skip_ws(p + n);
        if (*q == '\0' || *q == ',' || *q == ')') {
            return n;
        }
    }
    return 0;
}

static asm_value_t resolve_anon(asm_state_t *st, char sign, int count, bool *ok)
{
    asm_value_t v = { 0, false };
    struct anon_s *list = (st->pass == 2) ? st->anon_prev : st->anon;
    int n = (st->pass == 2) ? st->nanon_prev : st->nanon;
    int i;

    if (sign == '+') {
        for (i = 0; i < n; i++) {
            if (list[i].sign == '+' && list[i].count == count && list[i].line > st->line_no) {
                v.value = list[i].addr;
                v.known = true;
                return v;
            }
        }
    } else {
        for (i = n - 1; i >= 0; i--) {
            if (list[i].sign == '-' && list[i].count == count && list[i].line <= st->line_no) {
                v.value = list[i].addr;
                v.known = true;
                return v;
            }
        }
    }
    if (st->pass == 2) {
        asm_error(st, "anonymous label '%c' (x%d) not found", sign, count);
        *ok = false;
    }
    return v;
}

static void define_anon(asm_state_t *st, char sign, int count)
{
    if (st->nanon < MAX_ANON) {
        st->anon[st->nanon].sign = sign;
        st->anon[st->nanon].count = count;
        st->anon[st->nanon].addr = st->pc;
        st->anon[st->nanon].line = st->line_no;
        st->nanon++;
    } else {
        asm_error(st, "too many anonymous labels");
    }
}

static asm_value_t parse_unary(asm_state_t *st, const char **pp, bool *ok)
{
    const char *p = skip_ws(*pp);
    asm_value_t v;
    int alen = anon_ref_len(p);

    if (alen > 0) {
        v = resolve_anon(st, *p, alen, ok);
        *pp = p + alen;
        return v;
    }

    if (*p == '<' && p[1] != '<') {
        p++;
        v = parse_unary(st, &p, ok);
        v.value &= 0xff;
    } else if (*p == '>' && p[1] != '>') {
        p++;
        v = parse_unary(st, &p, ok);
        v.value = (v.value >> 8) & 0xff;
    } else if (*p == '-') {
        p++;
        v = parse_unary(st, &p, ok);
        v.value = -v.value;
    } else if (*p == '~') {
        p++;
        v = parse_unary(st, &p, ok);
        v.value = ~v.value;
    } else {
        v = parse_primary(st, &p, ok);
    }
    *pp = p;
    return v;
}

static asm_value_t parse_mul(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_unary(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        char op = *p;
        asm_value_t b;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        p++;
        b = parse_unary(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        if (op == '*') {
            a.value *= b.value;
        } else if (b.value == 0) {
            if (st->pass == 2) {
                asm_error(st, "division by zero");
                *ok = false;
            }
            a.value = 0;
        } else if (op == '/') {
            a.value /= b.value;
        } else {
            a.value %= b.value;
        }
    }
    return a;
}

static asm_value_t parse_add(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_mul(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        char op = *p;
        asm_value_t b;
        if (op != '+' && op != '-') {
            break;
        }
        p++;
        b = parse_mul(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        a.value = (op == '+') ? a.value + b.value : a.value - b.value;
    }
    return a;
}

static asm_value_t parse_shift(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_add(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        asm_value_t b;
        bool left;
        if (p[0] == '<' && p[1] == '<') {
            left = true;
        } else if (p[0] == '>' && p[1] == '>') {
            left = false;
        } else {
            break;
        }
        p += 2;
        b = parse_add(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        a.value = left ? (a.value << b.value) : (a.value >> b.value);
    }
    return a;
}

static asm_value_t parse_and(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_shift(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        asm_value_t b;
        if (*p != '&') {
            break;
        }
        p++;
        b = parse_shift(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        a.value &= b.value;
    }
    return a;
}

static asm_value_t parse_xor(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_and(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        asm_value_t b;
        if (*p != '^') {
            break;
        }
        p++;
        b = parse_and(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        a.value ^= b.value;
    }
    return a;
}

static asm_value_t parse_expr(asm_state_t *st, const char **pp, bool *ok)
{
    asm_value_t a = parse_xor(st, pp, ok);
    for (;;) {
        const char *p = skip_ws(*pp);
        asm_value_t b;
        if (*p != '|') {
            break;
        }
        p++;
        b = parse_xor(st, &p, ok);
        *pp = p;
        a.known = a.known && b.known;
        a.value |= b.value;
    }
    return a;
}

/* evaluate a complete expression string */
static asm_value_t eval_str(asm_state_t *st, const char *text, bool *ok)
{
    const char *p = text;
    asm_value_t v;

    *ok = true;
    v = parse_expr(st, &p, ok);
    p = skip_ws(p);
    if (*ok && *p != '\0') {
        asm_error(st, "unexpected '%s' in expression", p);
        *ok = false;
    }
    return v;
}

static void emit_byte(asm_state_t *st, long value)
{
    uint16_t addr = (uint16_t)(st->pc & 0xffff);

    if (st->pass == 2) {
        emudis_asm_result_t *r = st->res;
        if (st->pc > 0xffff || st->pc < 0) {
            asm_error(st, "program counter out of range ($%lX)", (unsigned long)st->pc);
        } else {
            if (r->used[addr]) {
                asm_error(st, "address $%04X is written twice", addr);
            }
            r->mem[addr] = (uint8_t)(value & 0xff);
            r->used[addr] = 1;
            r->bytes++;
            if (r->lowest < 0 || addr < r->lowest) {
                r->lowest = addr;
            }
            if (addr > r->highest) {
                r->highest = addr;
            }
        }
    }
    st->pc++;
}

/* find opcode for mnemonic + mode, prefer official opcodes */
static int find_opcode(const char *mnem, emudis_amode_t mode)
{
    int i;
    int illegal = -1;

    for (i = 0; i < 256; i++) {
        if (emudis_opcodes[i].mode == mode
            && g_ascii_strcasecmp(emudis_opcodes[i].mnemonic, mnem) == 0) {
            if (!emudis_opcodes[i].illegal) {
                return i;
            }
            if (illegal < 0) {
                illegal = i;
            }
        }
    }
    return illegal;
}

static bool is_mnemonic(const char *name)
{
    int i;

    if (strlen(name) != 3) {
        return false;
    }
    for (i = 0; i < 256; i++) {
        if (g_ascii_strcasecmp(emudis_opcodes[i].mnemonic, name) == 0) {
            return true;
        }
    }
    /* common aliases */
    return g_ascii_strcasecmp(name, "ASR") == 0 || g_ascii_strcasecmp(name, "AXS") == 0
        || g_ascii_strcasecmp(name, "ISB") == 0 || g_ascii_strcasecmp(name, "DCM") == 0
        || g_ascii_strcasecmp(name, "AHX") == 0 || g_ascii_strcasecmp(name, "XAA") == 0
        || g_ascii_strcasecmp(name, "KIL") == 0 || g_ascii_strcasecmp(name, "LSE") == 0;
}

static const char *mnemonic_alias(const char *name)
{
    if (g_ascii_strcasecmp(name, "ASR") == 0) { return "ALR"; }
    if (g_ascii_strcasecmp(name, "AXS") == 0) { return "SBX"; }
    if (g_ascii_strcasecmp(name, "ISB") == 0) { return "ISC"; }
    if (g_ascii_strcasecmp(name, "DCM") == 0) { return "DCP"; }
    if (g_ascii_strcasecmp(name, "AHX") == 0) { return "SHA"; }
    if (g_ascii_strcasecmp(name, "XAA") == 0) { return "ANE"; }
    if (g_ascii_strcasecmp(name, "KIL") == 0) { return "JAM"; }
    if (g_ascii_strcasecmp(name, "LSE") == 0) { return "SRE"; }
    return name;
}

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t ls = strlen(s);
    size_t lx = strlen(suffix);

    return ls >= lx && g_ascii_strcasecmp(s + ls - lx, suffix) == 0;
}

/* strip trailing whitespace in place */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/* remove all spaces outside quotes (for operand pattern matching) */
static void squeeze(char *s)
{
    char *d = s;
    bool q = false;

    for (; *s; s++) {
        if (*s == '"' || *s == '\'') {
            q = !q;
        }
        if (q || (*s != ' ' && *s != '\t')) {
            *d++ = *s;
        }
    }
    *d = '\0';
}

static void assemble_instruction(asm_state_t *st, const char *mnem_in, const char *operand_in)
{
    char operand[512];
    char inner[512];
    const char *mnem = mnemonic_alias(mnem_in);
    emudis_amode_t mode = AM_IMP;
    asm_value_t v = { 0, true };
    bool ok = true;
    bool is_branch;
    int op;
    size_t len;
    int line_idx = st->line_no - 1;

    g_strlcpy(operand, operand_in, sizeof operand);
    squeeze(operand);
    len = strlen(operand);

    op = find_opcode(mnem, AM_REL);
    is_branch = (op >= 0);

    if (len == 0) {
        mode = (find_opcode(mnem, AM_IMP) >= 0) ? AM_IMP : AM_ACC;
    } else if (g_ascii_strcasecmp(operand, "A") == 0 && find_opcode(mnem, AM_ACC) >= 0) {
        mode = AM_ACC;
    } else if (operand[0] == '#') {
        mode = AM_IMM;
        v = eval_str(st, operand + 1, &ok);
    } else if (is_branch) {
        mode = AM_REL;
        v = eval_str(st, operand, &ok);
    } else if (operand[0] == '(' && ends_with_ci(operand, ",X)")) {
        g_strlcpy(inner, operand + 1, sizeof inner);
        inner[strlen(inner) - 3] = '\0';
        mode = AM_IZX;
        v = eval_str(st, inner, &ok);
    } else if (operand[0] == '(' && ends_with_ci(operand, "),Y")) {
        g_strlcpy(inner, operand + 1, sizeof inner);
        inner[strlen(inner) - 3] = '\0';
        mode = AM_IZY;
        v = eval_str(st, inner, &ok);
    } else if (operand[0] == '(' && operand[len - 1] == ')'
               && find_opcode(mnem, AM_IND) >= 0) {
        g_strlcpy(inner, operand + 1, sizeof inner);
        inner[strlen(inner) - 1] = '\0';
        mode = AM_IND;
        v = eval_str(st, inner, &ok);
    } else if (ends_with_ci(operand, ",X") || ends_with_ci(operand, ",Y")) {
        bool x = (toupper((unsigned char)operand[len - 1]) == 'X');
        g_strlcpy(inner, operand, sizeof inner);
        inner[len - 2] = '\0';
        v = eval_str(st, inner, &ok);
        if (st->pass == 1) {
            st->force_abs[line_idx] = (!v.known || v.value > 0xff || v.value < 0
                                       || find_opcode(mnem, x ? AM_ZPX : AM_ZPY) < 0) ? 1 : 0;
        }
        mode = st->force_abs[line_idx] ? (x ? AM_ABX : AM_ABY) : (x ? AM_ZPX : AM_ZPY);
    } else {
        v = eval_str(st, operand, &ok);
        if (st->pass == 1) {
            st->force_abs[line_idx] = (!v.known || v.value > 0xff || v.value < 0
                                       || find_opcode(mnem, AM_ZP) < 0) ? 1 : 0;
        }
        mode = st->force_abs[line_idx] ? AM_ABS : AM_ZP;
    }

    op = find_opcode(mnem, mode);
    if (op < 0 && mode == AM_IMM && g_ascii_strcasecmp(mnem, "LAX") == 0) {
        op = 0xab;  /* LAX #imm is also known as LXA/OAL */
    }
    if (op < 0 && mode == AM_ACC) {
        op = find_opcode(mnem, AM_IMP);
        mode = AM_IMP;
    }
    if (op < 0) {
        asm_error(st, "addressing mode not supported by %s", mnem);
        /* keep the PC moving in a sensible way */
        st->pc += amode_sizes[mode];
        return;
    }

    if (st->pass == 2 && ok) {
        if (mode == AM_IMM && (v.value > 0xff || v.value < -128)) {
            asm_error(st, "immediate value out of range ($%lX)", (unsigned long)v.value);
        } else if ((mode == AM_ZP || mode == AM_ZPX || mode == AM_ZPY || mode == AM_IZX
                    || mode == AM_IZY) && (v.value > 0xff || v.value < 0)) {
            asm_error(st, "zero page address out of range ($%lX)", (unsigned long)v.value);
        } else if ((mode == AM_ABS || mode == AM_ABX || mode == AM_ABY || mode == AM_IND)
                   && (v.value > 0xffff || v.value < 0)) {
            asm_error(st, "address out of range ($%lX)", (unsigned long)v.value);
        }
    }

    if (mode == AM_REL) {
        long dist = v.value - (st->pc + 2);
        if (st->pass == 2 && ok && (dist < -128 || dist > 127)) {
            asm_error(st, "branch out of range (%ld bytes)", dist);
        }
        emit_byte(st, op);
        emit_byte(st, dist & 0xff);
        return;
    }

    emit_byte(st, op);
    if (amode_sizes[mode] >= 2) {
        emit_byte(st, v.value & 0xff);
    }
    if (amode_sizes[mode] == 3) {
        emit_byte(st, (v.value >> 8) & 0xff);
    }
}

/* split a comma separated argument list, honouring quotes and parentheses */
static GPtrArray *split_args(const char *s)
{
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    GString *cur = g_string_new(NULL);
    bool q = false;
    int depth = 0;

    for (; *s; s++) {
        if (*s == '"') {
            q = !q;
        }
        if (!q && *s == '(') {
            depth++;
        }
        if (!q && *s == ')') {
            depth--;
        }
        if (!q && depth == 0 && *s == ',') {
            g_ptr_array_add(args, g_strstrip(g_strdup(cur->str)));
            g_string_truncate(cur, 0);
            continue;
        }
        g_string_append_c(cur, *s);
    }
    if (cur->len > 0 || args->len > 0) {
        g_ptr_array_add(args, g_strstrip(g_strdup(cur->str)));
    }
    g_string_free(cur, TRUE);
    return args;
}

typedef enum text_mode_e { TEXT_RAW, TEXT_PETSCII, TEXT_SCREEN } text_mode_t;

static uint8_t convert_char(unsigned char c, text_mode_t mode)
{
    if (mode == TEXT_PETSCII) {
        if (c >= 'a' && c <= 'z') {
            return (uint8_t)(c - 'a' + 0x41);
        }
        if (c >= 'A' && c <= 'Z') {
            return (uint8_t)(c - 'A' + 0xc1);
        }
        return c;
    }
    if (mode == TEXT_SCREEN) {
        if (c >= 'a' && c <= 'z') {
            return (uint8_t)(c - 'a' + 1);
        }
        if (c >= 'A' && c <= 'Z') {
            return (uint8_t)(c - 'A' + 0x41);
        }
        if (c == '@') {
            return 0;
        }
        if (c >= 0x5b && c <= 0x5f) {
            return (uint8_t)(c - 0x40);
        }
        return c;
    }
    return c;
}

static void emit_data(asm_state_t *st, const char *argstr, int width, text_mode_t tmode)
{
    GPtrArray *args = split_args(argstr);
    guint i;

    if (args->len == 0) {
        asm_error(st, "missing data");
    }
    for (i = 0; i < args->len; i++) {
        const char *a = g_ptr_array_index(args, i);
        size_t len = strlen(a);
        if (len >= 2 && a[0] == '"' && a[len - 1] == '"') {
            size_t k;
            for (k = 1; k < len - 1; k++) {
                emit_byte(st, convert_char((unsigned char)a[k], tmode));
                if (width == 2) {
                    emit_byte(st, 0);
                }
            }
        } else if (len == 0) {
            asm_error(st, "empty data value");
        } else {
            bool ok;
            asm_value_t v = eval_str(st, a, &ok);
            if (st->pass == 2 && ok && width == 1 && (v.value > 0xff || v.value < -128)) {
                asm_error(st, "byte value out of range ($%lX)", (unsigned long)v.value);
            }
            emit_byte(st, v.value & 0xff);
            if (width == 2) {
                emit_byte(st, (v.value >> 8) & 0xff);
            }
        }
    }
    g_ptr_array_free(args, TRUE);
}

static bool dir_is(const char *d, const char *names)
{
    gchar **list = g_strsplit(names, " ", -1);
    bool found = false;
    int i;

    for (i = 0; list[i] != NULL; i++) {
        if (g_ascii_strcasecmp(d, list[i]) == 0) {
            found = true;
            break;
        }
    }
    g_strfreev(list);
    return found;
}

static void set_pc(asm_state_t *st, const char *expr)
{
    bool ok;
    asm_value_t v = eval_str(st, expr, &ok);

    if (!v.known && st->pass == 1) {
        asm_error(st, "origin must be known in pass 1");
    }
    if (ok) {
        if (v.value < 0 || v.value > 0xffff) {
            asm_error(st, "origin out of range");
        } else {
            st->pc = v.value;
        }
    }
}

static void assemble_line(asm_state_t *st, const char *raw)
{
    char line[1024];
    char work[1024];
    char ident[SYM_LEN];
    const char *p;
    long pc_before = st->pc;
    bool q = false;
    size_t i;

    /* strip comments (outside quotes) */
    g_strlcpy(line, raw, sizeof line);
    for (i = 0; line[i]; i++) {
        if (line[i] == '"') {
            q = !q;
        }
        if (!q && (line[i] == ';' || (line[i] == '/' && line[i + 1] == '/'))) {
            line[i] = '\0';
            break;
        }
    }
    rtrim(line);
    g_strlcpy(work, line, sizeof work);
    p = work;

    if (*skip_ws(p) == '\0') {
        if (st->pass == 2) {
            g_string_append_printf(st->list, "%-22s%s\n", "", raw);
        }
        return;
    }

    /* ACME anonymous label at column 0: "+", "++", "-", "--" */
    if (*p == '+' || *p == '-') {
        char sign = *p;
        int n = 0;
        while (p[n] == sign) {
            n++;
        }
        if (p[n] == ' ' || p[n] == '\t' || p[n] == '\0') {
            define_anon(st, sign, n);
            p += n;
        }
    }

    /* label at column 0 or "label:" */
    for (;;) {
        const char *s = skip_ws(p);
        const char *after;
        bool col0 = (s == p);

        if (!is_ident_start(*s) && *s != '!') {
            break;
        }
        if (*s == '!') {
            break;  /* ACME pseudo op */
        }
        after = read_ident(s, ident, sizeof ident);
        if (*after == ':') {
            sym_define(st, ident, st->pc);
            p = after + 1;
            continue;
        }
        /* "name = expr" or "name .equ expr" */
        {
            const char *a2 = skip_ws(after);
            if (*a2 == '=' && a2[1] != '=') {
                bool ok;
                asm_value_t v = eval_str(st, a2 + 1, &ok);
                if (st->pass == 2 && !v.known) {
                    asm_error(st, "cannot resolve value of '%s'", ident);
                }
                sym_define(st, ident, v.value);
                if (st->pass == 2) {
                    g_string_append_printf(st->list, "      = $%04lX       %s\n",
                                           (unsigned long)(v.value & 0xffff), raw);
                }
                return;
            }
            if (g_ascii_strncasecmp(a2, ".equ", 4) == 0 && (a2[4] == ' ' || a2[4] == '\t')) {
                bool ok;
                asm_value_t v = eval_str(st, a2 + 4, &ok);
                sym_define(st, ident, v.value);
                return;
            }
        }
        if (col0 && ident[0] != '.' && !is_mnemonic(ident)) {
            sym_define(st, ident, st->pc);
            p = after;
            continue;
        }
        break;
    }

    p = skip_ws(p);
    if (*p == '\0') {
        if (st->pass == 2) {
            g_string_append_printf(st->list, "%04lX%18s%s\n", (unsigned long)(st->pc & 0xffff), "", raw);
        }
        return;
    }

    if (*p == '*') {
        const char *e = skip_ws(p + 1);
        if (*e == '=') {
            set_pc(st, e + 1);
            if (st->pass == 2) {
                g_string_append_printf(st->list, "%04lX%18s%s\n", (unsigned long)(st->pc & 0xffff), "", raw);
            }
            return;
        }
    }

    if (*p == '.' || *p == '!') {
        char dir[SYM_LEN];
        const char *args;
        dir[0] = *p;
        args = read_ident(p + 1, dir + 1, sizeof dir - 1);
        args = skip_ws(args);

        if (dir_is(dir, ".org !org .pc !pc")) {
            set_pc(st, args);
        } else if (dir_is(dir, ".byte .by !byte !by .db !08 !8 .b")) {
            emit_data(st, args, 1, TEXT_RAW);
        } else if (dir_is(dir, ".word .wo !word !wo .dw !16 .w")) {
            emit_data(st, args, 2, TEXT_RAW);
        } else if (dir_is(dir, ".tx !text !tx .ascii !raw")) {
            emit_data(st, args, 1, TEXT_RAW);
        } else if (dir_is(dir, "!pet .petscii .pet")) {
            emit_data(st, args, 1, TEXT_PETSCII);
        } else if (dir_is(dir, "!scr .screen .scr .text")) {
            emit_data(st, args, 1, TEXT_SCREEN);
        } else if (dir_is(dir, ".fill !fill .res !skip .ds")) {
            GPtrArray *a = split_args(args);
            bool ok = true;
            long count = 0, val = 0;
            if (a->len >= 1) {
                asm_value_t v = eval_str(st, g_ptr_array_index(a, 0), &ok);
                if (!v.known && st->pass == 1) {
                    asm_error(st, "fill count must be known");
                }
                count = v.value;
            }
            if (a->len >= 2) {
                asm_value_t v = eval_str(st, g_ptr_array_index(a, 1), &ok);
                val = v.value;
            }
            if (count < 0 || count > 0x10000) {
                asm_error(st, "bad fill count");
                count = 0;
            }
            if (dir_is(dir, "!skip .res .ds") && a->len < 2) {
                st->pc += count;
            } else {
                long k;
                for (k = 0; k < count; k++) {
                    emit_byte(st, val);
                }
            }
            g_ptr_array_free(a, TRUE);
        } else if (dir_is(dir, "!cpu .cpu !to .encoding !sl .label .const .var .segment .file !zone !zn .zone")) {
            /* accepted and ignored */
            if (dir_is(dir, ".label .const .var")) {
                /* KickAss: .label name = value */
                char name[SYM_LEN];
                const char *r = read_ident(skip_ws(args), name, sizeof name);
                r = skip_ws(r);
                if (*r == '=') {
                    bool ok;
                    asm_value_t v = eval_str(st, r + 1, &ok);
                    sym_define(st, name, v.value);
                }
            }
        } else {
            asm_error(st, "unknown directive '%s'", dir);
        }
    } else if (is_ident_start(*p)) {
        char mnem[SYM_LEN];
        const char *operand = read_ident(p, mnem, sizeof mnem);
        if (!is_mnemonic(mnem)) {
            asm_error(st, "unknown instruction '%s'", mnem);
        } else {
            assemble_instruction(st, mnem, skip_ws(operand));
        }
    } else {
        asm_error(st, "syntax error");
    }

    if (st->pass == 2) {
        char bytes[32] = "";
        long n = st->pc - pc_before;
        long k;
        char *b = bytes;
        for (k = 0; k < n && k < 4; k++) {
            g_snprintf(b, 4, "%02X ", st->res->mem[(pc_before + k) & 0xffff]);
            b += 3;
        }
        if (n > 4) {
            g_strlcpy(b, "..", 3);
        }
        g_string_append_printf(st->list, "%04lX  %-15s %s\n",
                               (unsigned long)(pc_before & 0xffff), bytes, raw);
    }
}

bool emudis_assemble(const char *source, uint16_t origin, emudis_asm_result_t *result)
{
    asm_state_t st;
    gchar **lines;
    int i;

    memset(result, 0, sizeof *result);
    result->mem = lib_calloc(0x10000, 1);
    result->used = lib_calloc(0x10000, 1);
    result->lowest = -1;
    result->highest = -1;

    memset(&st, 0, sizeof st);
    st.syms = lib_calloc(MAX_SYMBOLS, sizeof(asm_symbol_t));
    st.msg = g_string_new(NULL);
    st.list = g_string_new(NULL);
    st.res = result;

    lines = g_strsplit(source != NULL ? source : "", "\n", -1);
    st.nlines = (int)g_strv_length(lines);
    st.force_abs = lib_calloc((size_t)st.nlines + 1, 1);

    st.anon = lib_calloc(MAX_ANON, sizeof *st.anon);
    st.anon_prev = lib_calloc(MAX_ANON, sizeof *st.anon_prev);

    for (st.pass = 1; st.pass <= 2; st.pass++) {
        if (st.pass == 2) {
            memcpy(st.anon_prev, st.anon, sizeof *st.anon * MAX_ANON);
            st.nanon_prev = st.nanon;
            st.nanon = 0;
        }
        st.pc = origin;
        for (i = 0; lines[i] != NULL; i++) {
            st.line_no = i + 1;
            assemble_line(&st, lines[i]);
        }
    }

    result->errors = st.errors;
    result->first_error_line = st.first_error_line;
    {
        GString *all = g_string_new(NULL);
        if (st.errors == 0) {
            if (result->bytes > 0) {
                g_string_append_printf(all, "OK: %d bytes assembled, $%04X-$%04X, %d symbols.\n\n",
                                       result->bytes, (unsigned int)result->lowest, (unsigned int)result->highest, st.nsyms);
            } else {
                g_string_append(all, "OK: nothing assembled.\n\n");
            }
        } else {
            g_string_append_printf(all, "%d error(s):\n%s\n", st.errors, st.msg->str);
        }
        g_string_append(all, st.list->str);
        if (st.nsyms > 0) {
            g_string_append(all, "\nSymbols:\n");
            for (i = 0; i < st.nsyms; i++) {
                g_string_append_printf(all, "  %-24s = $%04lX\n", st.syms[i].name,
                                       (unsigned long)(st.syms[i].value & 0xffff));
            }
        }
        result->listing = lib_strdup(all->str);
        g_string_free(all, TRUE);
    }

    g_strfreev(lines);
    g_string_free(st.msg, TRUE);
    g_string_free(st.list, TRUE);
    lib_free(st.syms);
    lib_free(st.force_abs);
    lib_free(st.anon);
    lib_free(st.anon_prev);
    return result->errors == 0;
}

void emudis_asm_result_free(emudis_asm_result_t *result)
{
    lib_free(result->mem);
    lib_free(result->used);
    lib_free(result->listing);
    memset(result, 0, sizeof *result);
}
