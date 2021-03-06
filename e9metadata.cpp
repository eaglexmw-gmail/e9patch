/*
 *        ___  _              _ 
 *   ___ / _ \| |_ ___   ___ | |
 *  / _ \ (_) | __/ _ \ / _ \| |
 * |  __/\__, | || (_) | (_) | |
 *  \___|  /_/ \__\___/ \___/|_|
 *                              
 * Copyright (C) 2020 National University of Singapore
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cassert>

/*
 * Convert a register to an argno.
 */
static int regToArgNo(x86_reg reg)
{
    switch (reg)
    {
        case X86_REG_DI: case X86_REG_DIL: case X86_REG_EDI: case X86_REG_RDI:
            return 0;
        case X86_REG_SI: case X86_REG_SIL: case X86_REG_ESI: case X86_REG_RSI:
            return 1;
        case X86_REG_DH: case X86_REG_DL:
        case X86_REG_DX: case X86_REG_EDX: case X86_REG_RDX:
            return 2;
        case X86_REG_CH: case X86_REG_CL:
        case X86_REG_CX: case X86_REG_ECX: case X86_REG_RCX:
            return 3;
        case X86_REG_R8B: case X86_REG_R8W: case X86_REG_R8D: case X86_REG_R8:
            return 4;
        case X86_REG_R9B: case X86_REG_R9W: case X86_REG_R9D: case X86_REG_R9:
            return 5;
        default:
            return INT32_MAX;
    }
}

/*
 * Save & restore a register used as an argument.
 */
static int8_t sendLoadArgReg(FILE *out, bool clean, int8_t offset,
    x86_reg reg, int argno)
{
    if (clean && reg == X86_REG_RAX)
    {
        // mov offset+48(%rsp),%rax
        fprintf(out, "%u,%u,%u,%u,%u,",
            0x48, 0x8b, 0x44, 0x24, 0x30 + offset);
        return offset;
    }

    int regno = regToArgNo(reg);
    if (argno <= regno)
        return offset;
    
    switch (regno)
    {
        case 0:
            fprintf(out, "%u,", 0x57);                  // push   %rdi
            break;
        case 1:
            fprintf(out, "%u,", 0x56);                  // push   %rsi
            break;
        case 2:
            fprintf(out, "%u,", 0x52);                  // push   %rdx
            break;
        case 3:
            fprintf(out, "%u,", 0x51);                  // push   %rcx
            break;
        case 4:
            fprintf(out, "%u,%u,", 0x41, 0x50);         // push   %r8
            break;
        case 5:
            fprintf(out, "%u,%u,", 0x41, 0x51);         // push   %r9
            break;
    }
    offset += 8;
    sendMovRSPR64(out, offset + 8 * regno, regno);
    return offset;
}

/*
 * Undo sendLoadArgReg().
 */
static void sendUnloadArgReg(FILE *out, x86_reg reg, int argno)
{
    int regno = regToArgNo(reg);
    if (argno <= regno)
        return;

    switch (regno)
    {
        case 0:
            fprintf(out, "%u,", 0x5f);                  // pop    %rdi
            break;
        case 1:
            fprintf(out, "%u,", 0x5e);                  // pop    %rsi
            break;
        case 2:
            fprintf(out, "%u,", 0x5a);                  // pop    %rdx
            break;
        case 3:
            fprintf(out, "%u,", 0x59);                  // pop    %rcx
            break;
        case 4:
            fprintf(out, "%u,%u,", 0x41, 0x58);         // pop    %r8
            break;
        case 5:
            fprintf(out, "%u,%u,", 0x41, 0x59);         // pop    %r9
            break;
    }
}

/*
 * Emits instructions to load the jump/call target into the corresponding
 * argno register.  Else, if I is not a jump/call instruction, load -1.
 */
static void sendLoadTargetMetadata(FILE *out, const cs_insn *I, bool clean,
    int argno)
{
    assert(argno < MAX_ARGNO);
    const cs_detail *detail = I->detail;
    const cs_x86 *x86       = &detail->x86;
    const cs_x86_op *op     = &x86->operands[0];

    fputc('[', out); 
    switch (I->id)
    {
        case X86_INS_CALL:
        case X86_INS_JMP:
        case X86_INS_JO: case X86_INS_JNO: case X86_INS_JB: case X86_INS_JAE:
        case X86_INS_JE: case X86_INS_JNE: case X86_INS_JBE: case X86_INS_JA:
        case X86_INS_JS: case X86_INS_JNS: case X86_INS_JP: case X86_INS_JNP:
        case X86_INS_JL: case X86_INS_JGE: case X86_INS_JLE: case X86_INS_JG:
        case X86_INS_JCXZ: case X86_INS_JECXZ: case X86_INS_JRCXZ:
            if (x86->op_count == 1)
                break;
            // Fallthrough:

        default:
        unknown:
        unsupported:
        {
            // This is NOT a jump or call, so the target is set to (-1):
            const uint8_t REX[MAX_ARGNO] =
                {0x48, 0x48, 0x48, 0x48, 0x49, 0x49};
            const uint8_t MODRM[MAX_ARGNO] =
                {0xc7, 0xc6, 0xc2, 0xc1, 0xc0, 0xc1};
           
            // mov $-1,%rarg
            fprintf(out, "%u,%u,%u,%u,%u,%u,%u]",
                REX[argno], 0xc7, MODRM[argno], 0xff, 0xff, 0xff, 0xff);
            return;
        }
    }

    switch (op->type)
    {
        case X86_OP_REG:
        {
            x86_reg reg = op->reg;
            int regno = regToArgNo(reg);
            if (argno < regno)
            {
                bool trivial = false;
                switch (reg)
                {
                    case X86_REG_RDI:
                        trivial = (argno == 0);
                        break;
                    case X86_REG_RSI:
                        trivial = (argno == 1);
                        break;
                    case X86_REG_RDX:
                        trivial = (argno == 2);
                        break;
                    case X86_REG_RCX:
                        trivial = (argno == 3);
                        break;
                    case X86_REG_R8:
                        trivial = (argno == 4);
                        break;
                    case X86_REG_R9:
                        trivial = (argno == 5);
                        break;
                    default:
                        break;
                }
                if (trivial)
                {
                    // Trivial case: value is already in correct register.
                    fputc(']', out);
                    return;
                }

                // Else, emit a mov %reg,%rarg
                uint8_t rex = 0;
                switch (reg)
                {
                    case X86_REG_RAX: case X86_REG_RBX: case X86_REG_RCX:
                    case X86_REG_RDX: case X86_REG_RDI: case X86_REG_RSI:
                    case X86_REG_RSP: case X86_REG_RBP:
                        rex = (argno < 4? 0x48: 0x49);
                        break;
                    case X86_REG_R8: case X86_REG_R9: case X86_REG_R10:
                    case X86_REG_R11: case X86_REG_R12: case X86_REG_R13:
                    case X86_REG_R14: case X86_REG_R15:
                        rex = (argno < 4? 0x4c: 0x4d);
                        break;
                    default:
                        error("unexpected register %d", reg);
                }
                fprintf(out, "%u,%u,", rex, 0x89);
                uint8_t mod = 0x3;
                const uint8_t RM[MAX_ARGNO] = {0x7, 0x6, 0x2, 0x1, 0x0, 0x1};
                uint8_t r = 0;
                switch (reg)
                {
                    case X86_REG_RAX: case X86_REG_R8:
                        r = 0x0; break;
                    case X86_REG_RCX: case X86_REG_R9:
                        r = 0x1; break;
                    case X86_REG_RDX: case X86_REG_R10:
                        r = 0x2; break;
                    case X86_REG_RBX: case X86_REG_R11:
                        r = 0x3; break;
                    case X86_REG_RSP: case X86_REG_R12:
                        r = 0x4; break;
                    case X86_REG_RBP: case X86_REG_R13:
                        r = 0x5; break;
                    case X86_REG_RSI: case X86_REG_R14:
                        r = 0x6; break;
                    case X86_REG_RDI: case X86_REG_R15:
                        r = 0x7; break;
                    default:
                        break;
                }
                uint8_t modrm = (mod << 6) | (r << 3) | RM[argno];
                fprintf(out, "%u,", modrm);
            }
            else if (clean && reg == X86_REG_RAX)
                sendMovRSPR64(out, 0, argno);
            else
            {
                int8_t offset = (clean? 8: 0);
                sendMovRSPR64(out, offset + 8 * regno, argno);
            }
            fputs("null]", out);
            return;
        }
        case X86_OP_MEM:
        {
            // This is an indirect jump/call.  Convert the instruction into a
            // mov instruction that loads the target in the correct register

            x86_reg base_reg = op->mem.base;
            x86_reg index_reg = op->mem.index;
            if (base_reg == X86_REG_RSP || base_reg == X86_REG_ESP ||
                index_reg == X86_REG_RSP || index_reg == X86_REG_ESP)
            {
                warning("failed to generate code for \"target\" argument for "
                    "instruction at address 0x%lx; memory operands using "
                    "%%rsp/%%esp are not yet supported", I->address);
                goto unsupported;
            }

            int8_t offset = (clean? 8: 0);
            offset = sendLoadArgReg(out, clean, offset, base_reg, argno);
            offset = sendLoadArgReg(out, clean, offset, index_reg, argno);

            // mov operand,%reg:
            const uint8_t REX[MAX_ARGNO] =
                {0x48, 0x48, 0x48, 0x48, 0x4c, 0x4c};
            uint8_t rex = REX[argno] | (x86->rex & 0x03);
            fprintf(out, "%u,%u,", rex, 0x8b);

            uint8_t modrm = x86->modrm;
            const uint8_t R[MAX_ARGNO] =
                {0x38, 0x30, 0x10, 0x08, 0x00, 0x08};
            modrm = (modrm & 0xc7) | R[argno];
            fprintf(out, "%u,", modrm);

            uint8_t mod  = (modrm & 0xc0) >> 6;
            uint8_t rm   = modrm & 0x7;
            uint8_t i    = x86->encoding.modrm_offset + 1;
            uint8_t base = 0;
            if (mod != 0x3 && rm == 0x4)        // have SIB?
            {
                uint8_t sib = x86->sib;
                fprintf(out, "%u,", sib);
                base = sib & 0x7;
                i++;
            }
            if (mod == 0x1 || mod == 0x2 ||
                    (mod == 0x0 && rm == 0x4 && base == 0x5))
            {
                fprintf(out, "{\"int%d\":", (mod == 0x1? 8: 32));
                sendInteger(out, (intptr_t)x86->disp);
                fputs("},", out);
            }
            else if (mod == 0x0 && rm == 0x5)
            {
                // Special handling for %rip-relative memory operands.
                intptr_t addr = I->address + I->size + x86->disp;
                fputs("{\"rel32\":", out);
                sendInteger(out, addr);
                fputs("},", out);
            }

            sendUnloadArgReg(out, index_reg, argno);
            sendUnloadArgReg(out, base_reg, argno);
            fputs("null]", out);
            return;
        }
        case X86_OP_IMM:
        {
            // This is a direct jump/call.  Emit an LEA that loads the target
            // into the correct register.

            // lea rel(%rip),%rarg
            intptr_t target = /*I->address + I->size +*/ op->imm;
            const uint8_t REX[MAX_ARGNO] =
                {0x48, 0x48, 0x48, 0x48, 0x4c, 0x4c};
            const uint8_t MODRM[MAX_ARGNO] =
                {0x3d, 0x35, 0x15, 0x0d, 0x05, 0x0d};
            fprintf(out, "%u,%u,%u,{\"rel32\":", REX[argno], 0x8d,
                MODRM[argno]);
            sendInteger(out, target);
            fputs("}]", out);
            return;
        }
        default:
            goto unknown;
    }
}

/*
 * Send string character data.
 */
static void sendStringCharData(FILE *out, char c)
{
	switch (c)
    {
        case '\\':
            fputs("\\\\", out);
            break;
        case '\"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        default:
            putc(c, out);
            break;
    }
}

/*
 * String asm string data.
 */
static void sendAsmStrData(FILE *out, const cs_insn *I,
    bool newline = false)
{
    fputc('\"', out);
	for (unsigned i = 0; I->mnemonic[i] != '\0'; i++)
        sendStringCharData(out, I->mnemonic[i]);
    if (I->op_str[0] != '\0')
    {
        sendStringCharData(out, ' ');
	    for (unsigned i = 0; I->op_str[i] != '\0'; i++)
            sendStringCharData(out, I->op_str[i]);
    }
    if (newline)
        sendStringCharData(out, '\n');
    fputc('\"', out);
}

/*
 * Send integer data.
 */
static void sendIntegerData(FILE *out, unsigned size, intptr_t i)
{
    assert(size == 8 || size == 16 || size == 32 || size == 64);
    fprintf(out, "{\"int%u\":", size);
    sendInteger(out, i);
    fputc('}', out);
}

/*
 * Send bytes data.
 */
static void sendBytesData(FILE *out, const uint8_t *bytes, size_t len)
{
    fputc('[', out);
    for (size_t i = 0; i < len; i++)
        fprintf(out, "%u%c", bytes[i], (i+1 < len? ',': ']'));
}

/*
 * Build a metadata value (string).
 */
static const char *buildMetadataString(FILE *out, char *buf, long *pos)
{
    fputc('\0', out);
    if (ferror(out))
        error("failed to build metadata string: %s", strerror(errno));
    
    const char *str = buf + *pos;
    *pos = ftell(out);

    return str;
}

/*
 * Build metadata.
 */
static Metadata *buildMetadata(const Action *action, const cs_insn *I,
    Metadata *metadata, char *buf, size_t size)
{
    if (action == nullptr || action->kind == ACTION_PASSTHRU ||
            action->kind == ACTION_TRAP || action->kind == ACTION_PLUGIN ||
            (action->kind == ACTION_CALL && action->args.size() == 0))
    {
        return nullptr;
    }

    FILE *out = fmemopen(buf, size, "w");
    if (out == nullptr)
        error("failed to open metadata stream for buffer of size %zu: %s",
            size, strerror(errno));
    setvbuf(out, NULL, _IONBF, 0);
    long pos = 0;

    switch (action->kind)
    {
        case ACTION_PRINT:
        {
            sendAsmStrData(out, I, /*newline=*/true);
            const char *asm_str = buildMetadataString(out, buf, &pos);
            intptr_t len = 1 + strlen(I->mnemonic) +
                (I->op_str[0] == '\0'? 0: 1 + strlen(I->op_str));
            sendIntegerData(out, 32, len);
            const char *asm_str_len = buildMetadataString(out, buf, &pos);

            metadata[0].name = "$asmStr";
            metadata[0].data = asm_str;
            metadata[1].name = "$asmStrLen";
            metadata[1].data = asm_str_len;
            metadata[2].name = nullptr;
            metadata[2].data = nullptr;
            
            break;
        }
        case ACTION_CALL:
        {
            const char *asm_str     = nullptr;
            const char *asm_str_len = nullptr;
            const char *bytes       = nullptr;
            const char *bytes_len   = nullptr;
            int i = 0, argno = 0;
            for (auto &arg: action->args)
            {
                switch (arg.kind)
                {
                    case ARGUMENT_ASM_STR:
                        if (asm_str == nullptr)
                        {
                            sendAsmStrData(out, I);
                            asm_str = buildMetadataString(out, buf, &pos);
                            metadata[i].name = "$asmStr";
                            metadata[i].data = asm_str;
                            i++;
                        }
                        break;

                    case ARGUMENT_ASM_STR_LEN:
                        if (asm_str_len == nullptr)
                        {
                            intptr_t len = strlen(I->mnemonic) +
                                (I->op_str[0] == '\0'? 0:
                                 1 + strlen(I->op_str));
                            sendIntegerData(out, 32, len);
                            asm_str_len = buildMetadataString(out, buf, &pos);
                            metadata[i].name = "$asmStrLen";
                            metadata[i].data = asm_str_len;
                            i++;
                        }
                        break;

                    case ARGUMENT_BYTES:
                        if (bytes == nullptr)
                        {
                            sendBytesData(out, I->bytes, I->size);
                            bytes = buildMetadataString(out, buf, &pos);
                            metadata[i].name = "$bytes";
                            metadata[i].data = bytes;
                            i++;
                        }
                        break;

                    case ARGUMENT_BYTES_LEN:
                        if (bytes_len == nullptr)
                        {
                            sendIntegerData(out, 32, I->size);
                            bytes_len = buildMetadataString(out, buf, &pos);
                            metadata[i].name = "$bytesLen";
                            metadata[i].data = bytes_len;
                            i++;
                        }
                        break;

                    case ARGUMENT_TARGET:
                    {
                        sendLoadTargetMetadata(out, I, action->clean, argno);
                        const char *load_target =
                            buildMetadataString(out, buf, &pos);
                        metadata[i].name = getLoadTargetName(argno);
                        metadata[i].data = load_target;
                        i++;
                        break;
                    }
                    default:
                        break;
                }
                argno++;
            }

            metadata[i].name = nullptr;
            metadata[i].data = nullptr;
            break;
        }

        default:
            break;
    }

    fclose(out);
    return metadata;
}

