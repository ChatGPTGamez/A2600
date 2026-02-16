#include "core/cpu6507.h"
#include <string.h>
#include <SDL3/SDL.h>

static inline void setZN(CPU6507* c, u8 v) {
    if (v == 0) c->P |= CPU_FLAG_Z; else c->P &= (u8)~CPU_FLAG_Z;
    if (v & 0x80) c->P |= CPU_FLAG_N; else c->P &= (u8)~CPU_FLAG_N;
}

static inline u8  R(void* ctx, cpu_read_fn rfn, u16 a) { return rfn(ctx, a); }
static inline void W(void* ctx, cpu_write_fn wfn, u16 a, u8 v) { wfn(ctx, a, v); }

static inline u8 pull(CPU6507* c, void* ctx, cpu_read_fn rfn) {
    c->S++;
    return R(ctx, rfn, (u16)(0x0100 | c->S));
}
static inline void push(CPU6507* c, void* ctx, cpu_write_fn wfn, u8 v) {
    W(ctx, wfn, (u16)(0x0100 | c->S), v);
    c->S--;
}

static inline u16 read16(void* ctx, cpu_read_fn rfn, u16 addr) {
    u8 lo = R(ctx, rfn, addr);
    u8 hi = R(ctx, rfn, (u16)(addr + 1));
    return (u16)lo | ((u16)hi << 8);
}

static inline u16 imm(CPU6507* c) { return c->PC++; }
static inline u16 zp (CPU6507* c, void* ctx, cpu_read_fn rfn) { (void)ctx; return R(ctx, rfn, c->PC++); }
static inline u16 zpx(CPU6507* c, void* ctx, cpu_read_fn rfn) { u8 a = R(ctx, rfn, c->PC++); return (u8)(a + c->X); }
static inline u16 zpy(CPU6507* c, void* ctx, cpu_read_fn rfn) { u8 a = R(ctx, rfn, c->PC++); return (u8)(a + c->Y); }

static inline u16 abs_(CPU6507* c, void* ctx, cpu_read_fn rfn) {
    u16 lo = R(ctx, rfn, c->PC++);
    u16 hi = R(ctx, rfn, c->PC++);
    return (u16)(lo | (hi << 8));
}
static inline u16 absx(CPU6507* c, void* ctx, cpu_read_fn rfn, bool* page_cross) {
    u16 base = abs_(c, ctx, rfn);
    u16 a = (u16)(base + c->X);
    if (page_cross) *page_cross = ((base ^ a) & 0xFF00) != 0;
    return a;
}
static inline u16 absy(CPU6507* c, void* ctx, cpu_read_fn rfn, bool* page_cross) {
    u16 base = abs_(c, ctx, rfn);
    u16 a = (u16)(base + c->Y);
    if (page_cross) *page_cross = ((base ^ a) & 0xFF00) != 0;
    return a;
}
static inline u16 indx(CPU6507* c, void* ctx, cpu_read_fn rfn) {
    u8 zpaddr = (u8)(R(ctx, rfn, c->PC++) + c->X);
    u8 lo = R(ctx, rfn, zpaddr);
    u8 hi = R(ctx, rfn, (u8)(zpaddr + 1));
    return (u16)lo | ((u16)hi << 8);
}
static inline u16 indy(CPU6507* c, void* ctx, cpu_read_fn rfn, bool* page_cross) {
    u8 zpaddr = R(ctx, rfn, c->PC++);
    u8 lo = R(ctx, rfn, zpaddr);
    u8 hi = R(ctx, rfn, (u8)(zpaddr + 1));
    u16 base = (u16)lo | ((u16)hi << 8);
    u16 a = (u16)(base + c->Y);
    if (page_cross) *page_cross = ((base ^ a) & 0xFF00) != 0;
    return a;
}

static inline u8 asl_u8(CPU6507* c, u8 v) {
    if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
    v = (u8)(v << 1);
    setZN(c, v);
    return v;
}
static inline u8 lsr_u8(CPU6507* c, u8 v) {
    if (v & 0x01) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
    v = (u8)(v >> 1);
    setZN(c, v);
    return v;
}
static inline u8 rol_u8(CPU6507* c, u8 v) {
    u8 carry = (c->P & CPU_FLAG_C) ? 1 : 0;
    if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
    v = (u8)((v << 1) | carry);
    setZN(c, v);
    return v;
}
static inline u8 ror_u8(CPU6507* c, u8 v) {
    u8 carry = (c->P & CPU_FLAG_C) ? 0x80 : 0;
    if (v & 0x01) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
    v = (u8)((v >> 1) | carry);
    setZN(c, v);
    return v;
}

static inline void adc(CPU6507* c, u8 v) {
    u8 a = c->A;
    u8 carry_in = (c->P & CPU_FLAG_C) ? 1 : 0;

    u16 sum_bin = (u16)a + (u16)v + (u16)carry_in;
    u8 res_bin = (u8)sum_bin;

    if (((a ^ res_bin) & (v ^ res_bin) & 0x80) != 0) c->P |= CPU_FLAG_V;
    else c->P &= (u8)~CPU_FLAG_V;

    if (c->P & CPU_FLAG_D) {
        u16 sum = (u16)a + (u16)v + (u16)carry_in;
        if (((a & 0x0F) + (v & 0x0F) + carry_in) > 9) sum += 0x06;
        if (sum > 0x99) { sum += 0x60; c->P |= CPU_FLAG_C; }
        else { c->P &= (u8)~CPU_FLAG_C; }

        c->A = (u8)sum;
        setZN(c, c->A);
    } else {
        if (sum_bin > 0xFF) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
        c->A = res_bin;
        setZN(c, c->A);
    }
}

static inline void sbc(CPU6507* c, u8 v) {
    u8 a = c->A;
    u8 carry_in = (c->P & CPU_FLAG_C) ? 1 : 0;

    u16 diff_bin = (u16)a - (u16)v - (u16)(carry_in ? 0 : 1);
    u8 res_bin = (u8)diff_bin;

    if (((a ^ v) & (a ^ res_bin) & 0x80) != 0) c->P |= CPU_FLAG_V;
    else c->P &= (u8)~CPU_FLAG_V;

    if (c->P & CPU_FLAG_D) {
        int al = (a & 0x0F) - (v & 0x0F) - (carry_in ? 0 : 1);
        int ah = (a >> 4) - (v >> 4);

        if (al < 0) { al -= 6; ah -= 1; }
        if (ah < 0) { ah -= 6; }

        u8 result = (u8)(((ah << 4) & 0xF0) | (al & 0x0F));
        if (diff_bin < 0x100) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;

        c->A = result;
        setZN(c, c->A);
    } else {
        if (diff_bin < 0x100) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C;
        c->A = res_bin;
        setZN(c, c->A);
    }
}

static inline void cmp_u8(CPU6507* c, u8 lhs, u8 rhs) {
    u16 diff = (u16)lhs - (u16)rhs;
    if (lhs >= rhs) c->P |= CPU_FLAG_C;
    else c->P &= (u8)~CPU_FLAG_C;
    setZN(c, (u8)diff);
}

void cpu6507_reset(CPU6507* c, void* ctx, cpu_read_fn rfn, cpu_write_fn wfn) {
    (void)wfn;
    memset(c, 0, sizeof(*c));
    c->S = 0xFD;
    c->P = (u8)(CPU_FLAG_U | CPU_FLAG_I);
    c->PC = read16(ctx, rfn, 0xFFFC);
}

int cpu6507_step(CPU6507* c, void* ctx, cpu_read_fn rfn, cpu_write_fn wfn) {
    if (c->jammed) return 1;
    if (c->stalled) return 1;

    u8 op = R(ctx, rfn, c->PC++);
    int cycles = 2;
    bool pcross = false;

    #define READ(a)  R(ctx, rfn, (a))
    #define WRITE(a,v) W(ctx, wfn, (a), (v))
    #define LD(reg,val) do { (reg)=(val); setZN(c,(reg)); } while(0)

    #define BR(cond) do { \
        s8 rel = (s8)READ(imm(c)); \
        if (cond) { \
            u16 old = c->PC; \
            c->PC = (u16)(c->PC + (s32)rel); \
            cycles = 3; \
            if (((old ^ c->PC) & 0xFF00) != 0) cycles++; \
        } else { \
            cycles = 2; \
        } \
    } while(0)

    switch (op) {
        // --- ADC ---
        case 0x69: adc(c, READ(imm(c))); cycles=2; break;
        case 0x65: adc(c, READ(zp(c,ctx,rfn))); cycles=3; break;
        case 0x75: adc(c, READ(zpx(c,ctx,rfn))); cycles=4; break;
        case 0x6D: adc(c, READ(abs_(c,ctx,rfn))); cycles=4; break;
        case 0x7D: adc(c, READ(absx(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0x79: adc(c, READ(absy(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0x61: adc(c, READ(indx(c,ctx,rfn))); cycles=6; break;
        case 0x71: adc(c, READ(indy(c,ctx,rfn,&pcross))); cycles=5+pcross; break;

        // --- SBC ---
        case 0xE9: sbc(c, READ(imm(c))); cycles=2; break;
        case 0xE5: sbc(c, READ(zp(c,ctx,rfn))); cycles=3; break;
        case 0xF5: sbc(c, READ(zpx(c,ctx,rfn))); cycles=4; break;
        case 0xED: sbc(c, READ(abs_(c,ctx,rfn))); cycles=4; break;
        case 0xFD: sbc(c, READ(absx(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0xF9: sbc(c, READ(absy(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0xE1: sbc(c, READ(indx(c,ctx,rfn))); cycles=6; break;
        case 0xF1: sbc(c, READ(indy(c,ctx,rfn,&pcross))); cycles=5+pcross; break;

        // --- Shifts on A (enough to get you past common init code) ---
        case 0x0A: c->A = asl_u8(c, c->A); cycles=2; break;
        case 0x4A: c->A = lsr_u8(c, c->A); cycles=2; break;
        case 0x2A: c->A = rol_u8(c, c->A); cycles=2; break;
        case 0x6A: c->A = ror_u8(c, c->A); cycles=2; break;

        case 0x06: { u16 a = zp(c,ctx,rfn); u8 v = asl_u8(c, READ(a)); WRITE(a, v); cycles = 5; } break;
        case 0x0E: { u16 a = abs_(c,ctx,rfn); u8 v = asl_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x16: { u16 a = zpx(c,ctx,rfn); u8 v = asl_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x1E: { u16 a = absx(c,ctx,rfn,NULL); u8 v = asl_u8(c, READ(a)); WRITE(a, v); cycles = 7; } break;
        case 0x4E: { u16 a = abs_(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x56: { u16 a = zpx(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x5E: { u16 a = absx(c,ctx,rfn,NULL); u8 v = lsr_u8(c, READ(a)); WRITE(a, v); cycles = 7; } break;
        case 0x2E: { u16 a = abs_(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x36: { u16 a = zpx(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x3E: { u16 a = absx(c,ctx,rfn,NULL); u8 v = rol_u8(c, READ(a)); WRITE(a, v); cycles = 7; } break;
        case 0x76: { u16 a = zpx(c,ctx,rfn); u8 v = ror_u8(c, READ(a)); WRITE(a, v); cycles = 6; } break;
        case 0x7E: { u16 a = absx(c,ctx,rfn,NULL); u8 v = ror_u8(c, READ(a)); WRITE(a, v); cycles = 7; } break;

        // --- LDA (subset) ---
        case 0xA9: LD(c->A, READ(imm(c))); cycles=2; break;
        case 0xA5: LD(c->A, READ(zp(c,ctx,rfn))); cycles=3; break;
        case 0xB5: LD(c->A, READ(zpx(c,ctx,rfn))); cycles=4; break;
        case 0xAD: LD(c->A, READ(abs_(c,ctx,rfn))); cycles=4; break;
        case 0xBD: LD(c->A, READ(absx(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0xB9: LD(c->A, READ(absy(c,ctx,rfn,&pcross))); cycles=4+pcross; break;
        case 0xA1: LD(c->A, READ(indx(c,ctx,rfn))); cycles=6; break;
        case 0xB1: LD(c->A, READ(indy(c,ctx,rfn,&pcross))); cycles=5+pcross; break;
        case 0xA3: { u8 v = READ(indx(c,ctx,rfn)); c->A = v; c->X = v; setZN(c, v); cycles = 6; } break; // LAX (zp,X)
        case 0xA7: { u8 v = READ(zp(c,ctx,rfn)); c->A = v; c->X = v; setZN(c, v); cycles = 3; } break; // LAX zp
        case 0xAF: { u8 v = READ(abs_(c,ctx,rfn)); c->A = v; c->X = v; setZN(c, v); cycles = 4; } break; // LAX abs
        case 0xB3: { u8 v = READ(indy(c,ctx,rfn,&pcross)); c->A = v; c->X = v; setZN(c, v); cycles = 5 + pcross; } break; // LAX (zp),Y
        case 0xB7: { u8 v = READ(zpy(c,ctx,rfn)); c->A = v; c->X = v; setZN(c, v); cycles = 4; } break; // LAX zp,Y
        case 0xBF: { u8 v = READ(absy(c,ctx,rfn,&pcross)); c->A = v; c->X = v; setZN(c, v); cycles = 4 + pcross; } break; // LAX abs,Y

        // --- JMP/JSR/RTS/RTI/BRK ---
        case 0x4C: c->PC = abs_(c,ctx,rfn); cycles=3; break;
        case 0x6C: {
            u16 ptr = abs_(c,ctx,rfn);
            u8 lo = READ(ptr);
            u8 hi = READ((u16)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
            c->PC = (u16)lo | ((u16)hi << 8);
            cycles=5;
        } break;

        case 0x20: {
            u16 addr = abs_(c,ctx,rfn);
            u16 ret = (u16)(c->PC - 1);
            push(c,ctx,wfn,(u8)(ret >> 8));
            push(c,ctx,wfn,(u8)(ret & 0xFF));
            c->PC = addr;
            cycles=6;
        } break;

        case 0x60: {
            u8 lo = pull(c,ctx,rfn);
            u8 hi = pull(c,ctx,rfn);
            c->PC = (u16)(((u16)hi << 8) | lo);
            c->PC++;
            cycles=6;
        } break;

        case 0x40: {
            c->P = (u8)((pull(c,ctx,rfn) & (u8)~CPU_FLAG_B) | CPU_FLAG_U);
            u8 lo = pull(c,ctx,rfn);
            u8 hi = pull(c,ctx,rfn);
            c->PC = (u16)(((u16)hi << 8) | lo);
            cycles=6;
        } break;

        case 0x00: {
            c->PC++;
            push(c,ctx,wfn,(u8)(c->PC >> 8));
            push(c,ctx,wfn,(u8)(c->PC & 0xFF));
            push(c,ctx,wfn,(u8)(c->P | CPU_FLAG_B | CPU_FLAG_U));
            c->P |= CPU_FLAG_I;
            c->PC = read16(ctx, rfn, 0xFFFE);
            cycles=7;
        } break;

        // --- Branches ---
        case 0x10: BR(!(c->P & CPU_FLAG_N)); break;
        case 0x30: BR( (c->P & CPU_FLAG_N)); break;
        case 0x50: BR(!(c->P & CPU_FLAG_V)); break;
        case 0x70: BR( (c->P & CPU_FLAG_V)); break;
        case 0x90: BR(!(c->P & CPU_FLAG_C)); break;
        case 0xB0: BR( (c->P & CPU_FLAG_C)); break;
        case 0xD0: BR(!(c->P & CPU_FLAG_Z)); break;
        case 0xF0: BR( (c->P & CPU_FLAG_Z)); break;

        // --- Status flags (this fixes your opcode 0x78 crash, and prevents the next ones) ---
        case 0x18: c->P &= (u8)~CPU_FLAG_C; cycles=2; break; // CLC
        case 0x38: c->P |=  CPU_FLAG_C; cycles=2; break;     // SEC
        case 0x58: c->P &= (u8)~CPU_FLAG_I; cycles=2; break; // CLI
        case 0x78: c->P |=  CPU_FLAG_I; cycles=2; break;     // SEI  <-- YOUR CRASH
        case 0xB8: c->P &= (u8)~CPU_FLAG_V; cycles=2; break; // CLV
        case 0xD8: c->P &= (u8)~CPU_FLAG_D; cycles=2; break; // CLD
        case 0xF8: c->P |=  CPU_FLAG_D; cycles=2; break;     // SED

        // --- Stack basics often used by init code ---
        case 0x48: push(c,ctx,wfn,c->A); cycles=3; break;                 // PHA
        case 0x68: c->A = pull(c,ctx,rfn); setZN(c,c->A); cycles=4; break;// PLA
        case 0x08: push(c,ctx,wfn,(u8)(c->P | CPU_FLAG_B | CPU_FLAG_U)); cycles=3; break; // PHP
        case 0x28: c->P = (u8)((pull(c,ctx,rfn) & (u8)~CPU_FLAG_B) | CPU_FLAG_U); cycles=4; break; // PLP

        case 0xEA: cycles=2; break; // NOP
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: cycles = 2; break; // undocumented NOP
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: (void)READ(imm(c)); cycles = 2; break;
        case 0x04: case 0x44: case 0x64: (void)READ(zp(c,ctx,rfn)); cycles = 3; break;
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: (void)READ(zpx(c,ctx,rfn)); cycles = 4; break;
        case 0x0C: (void)READ(abs_(c,ctx,rfn)); cycles = 4; break;
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: (void)READ(absx(c,ctx,rfn,NULL)); cycles = 4; break;

        case 0xA2: LD(c->X, READ(imm(c))); cycles = 2; break;  // LDX #imm
        case 0xA6: LD(c->X, READ(zp(c,ctx,rfn))); cycles = 3; break;   // LDX zp
        case 0xB6: LD(c->X, READ(zpy(c,ctx,rfn))); cycles = 4; break;  // LDX zp,Y
        case 0xAE: LD(c->X, READ(abs_(c,ctx,rfn))); cycles = 4; break; // LDX abs
        case 0xBE: LD(c->X, READ(absy(c,ctx,rfn,&pcross))); cycles = 4 + pcross; break; // LDX abs,Y
        case 0x9A: c->S = c->X; cycles = 2; break;  // TXS

        case 0xE8:
            c->X++;
            setZN(c, c->X);
            cycles = 2;
        break;  // INX

        case 0x95:
            WRITE(zpx(c,ctx,rfn), c->A);
            cycles = 4;
        break;  // STA zp,X

        case 0xCA:
            c->X--;
            setZN(c, c->X);
            cycles = 2;
        break;  // DEX

        case 0x86:
            WRITE(zp(c,ctx,rfn), c->X);
            cycles = 3;
        break;  // STX zp

        case 0x8D:
            WRITE(abs_(c,ctx,rfn), c->A);
            cycles = 4;
        break;  // STA abs
        case 0x81: WRITE(indx(c,ctx,rfn), c->A); cycles = 6; break;
        case 0x91: WRITE(indy(c,ctx,rfn,NULL), c->A); cycles = 6; break;
        case 0x9D: WRITE(absx(c,ctx,rfn,NULL), c->A); cycles = 5; break;
        case 0x8C: WRITE(abs_(c,ctx,rfn), c->Y); cycles = 4; break;
        case 0x83: WRITE(indx(c,ctx,rfn), (u8)(c->A & c->X)); cycles = 6; break; // SAX
        case 0x87: WRITE(zp(c,ctx,rfn), (u8)(c->A & c->X)); cycles = 3; break; // SAX
        case 0x8F: WRITE(abs_(c,ctx,rfn), (u8)(c->A & c->X)); cycles = 4; break; // SAX
        case 0x97: WRITE(zpy(c,ctx,rfn), (u8)(c->A & c->X)); cycles = 4; break; // SAX

        case 0x85:
            WRITE(zp(c,ctx,rfn), c->A);
            cycles = 3;
        break;  // STA zp

        case 0xE6: {
            u16 a = zp(c,ctx,rfn);
            u8 v = (u8)(READ(a) + 1);
            WRITE(a, v);
            setZN(c, v);
            cycles = 5;
        } break;  // INC zp

        case 0xA0:
            LD(c->Y, READ(imm(c)));
            cycles = 2;
            break;  // LDY #imm
        case 0xAC: LD(c->Y, READ(abs_(c,ctx,rfn))); cycles = 4; break;
        case 0xBC: LD(c->Y, READ(absx(c,ctx,rfn,&pcross))); cycles = 4 + pcross; break;

        case 0xC9: {
            u8 v = READ(imm(c));
            cmp_u8(c, c->A, v);
            cycles = 2;
        } break;  // CMP #imm

        case 0xC5: {
            u8 v = READ(zp(c,ctx,rfn));
            cmp_u8(c, c->A, v);
            cycles = 3;
        } break;  // CMP zp
        case 0xC1: { u8 v = READ(indx(c,ctx,rfn)); cmp_u8(c, c->A, v); cycles = 6; } break;
        case 0xCD: { u8 v = READ(abs_(c,ctx,rfn)); cmp_u8(c, c->A, v); cycles = 4; } break;
        case 0xD1: { u8 v = READ(indy(c,ctx,rfn,&pcross)); cmp_u8(c, c->A, v); cycles = 5 + pcross; } break;
        case 0xDD: { u8 v = READ(absx(c,ctx,rfn,&pcross)); cmp_u8(c, c->A, v); cycles = 4 + pcross; } break;
        case 0x84:
            WRITE(zp(c,ctx,rfn), c->Y);
            cycles = 3;
            break;  // STY zp
        case 0x88:
            c->Y--;
            setZN(c, c->Y);
            cycles = 2;
            break;  // DEY

        case 0x29:
            c->A &= READ(imm(c));
            setZN(c, c->A);
            cycles = 2;
            break;  // AND #imm
        case 0x49:
            c->A ^= READ(imm(c));
            setZN(c, c->A);
            cycles = 2;
            break;  // EOR #imm
        case 0x41: c->A ^= READ(indx(c,ctx,rfn)); setZN(c, c->A); cycles = 6; break;
        case 0x4D: c->A ^= READ(abs_(c,ctx,rfn)); setZN(c, c->A); cycles = 4; break;
        case 0x51: c->A ^= READ(indy(c,ctx,rfn,&pcross)); setZN(c, c->A); cycles = 5 + pcross; break;
        case 0x55: c->A ^= READ(zpx(c,ctx,rfn)); setZN(c, c->A); cycles = 4; break;
        case 0x59: c->A ^= READ(absy(c,ctx,rfn,&pcross)); setZN(c, c->A); cycles = 4 + pcross; break;
        case 0x5D: c->A ^= READ(absx(c,ctx,rfn,&pcross)); setZN(c, c->A); cycles = 4 + pcross; break;
case 0xC6: {
    u16 a = zp(c,ctx,rfn);
    u8 v = READ(a);
    v--;
    WRITE(a, v);
    setZN(c, v);
    cycles = 5;
} break;  // DEC zp

case 0xC8:
    c->Y++;
    setZN(c, c->Y);
    cycles = 2;
    break;  // INY
case 0x05:
    c->A |= READ(zp(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 3;
    break;  // ORA zp
case 0xA8:
    c->Y = c->A;
    setZN(c, c->Y);
    cycles = 2;
    break;  // TAY
case 0x24: {
    u8 v = READ(zp(c,ctx,rfn));
    u8 r = (u8)(c->A & v);

    if (r == 0) c->P |= CPU_FLAG_Z;
    else        c->P &= (u8)~CPU_FLAG_Z;

    c->P = (u8)((c->P & ~(CPU_FLAG_N | CPU_FLAG_V)) |
                (v & (CPU_FLAG_N | CPU_FLAG_V)));

    cycles = 3;
} break;  // BIT zp
case 0x66: {
    u16 a = zp(c,ctx,rfn);
    u8 v = READ(a);

    u8 carry_in = (c->P & CPU_FLAG_C) ? 0x80 : 0x00;
    if (v & 0x01) c->P |= CPU_FLAG_C;
    else          c->P &= (u8)~CPU_FLAG_C;

    v = (u8)((v >> 1) | carry_in);
    WRITE(a, v);
    setZN(c, v);

    cycles = 5;
} break;  // ROR zp
case 0xA4:
    LD(c->Y, READ(zp(c,ctx,rfn)));
    cycles = 3;
    break;  // LDY zp
case 0xE0: {
    u8 v = READ(imm(c));
    cmp_u8(c, c->X, v);
    cycles = 2;
} break;  // CPX #imm
case 0x25:
    c->A &= READ(zp(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 3;
    break;  // AND zp
case 0x8A:
    c->A = c->X;
    setZN(c, c->A);
    cycles = 2;
    break;  // TXA
case 0xE4: {
    u8 v = READ(zp(c,ctx,rfn));
    cmp_u8(c, c->X, v);
    cycles = 3;
} break;  // CPX zp
case 0xEC: { u8 v = READ(abs_(c,ctx,rfn)); cmp_u8(c, c->X, v); cycles = 4; } break;
case 0xC0: {
    u8 v = READ(imm(c));
    cmp_u8(c, c->Y, v);
    cycles = 2;
} break;  // CPY #imm
case 0xCC: { u8 v = READ(abs_(c,ctx,rfn)); cmp_u8(c, c->Y, v); cycles = 4; } break;
case 0xAA:
    c->X = c->A;
    setZN(c, c->X);
    cycles = 2;
    break;  // TAX
case 0x98:
    c->A = c->Y;
    setZN(c, c->A);
    cycles = 2;
    break;  // TYA
case 0xD5: {
    u8 v = READ(zpx(c,ctx,rfn));
    cmp_u8(c, c->A, v);
    cycles = 4;
} break;  // CMP zp,X
case 0x8E:
    WRITE(abs_(c,ctx,rfn), c->X);
    cycles = 4;
    break;  // STX abs
case 0x2C: {
    u8 v = READ(abs_(c,ctx,rfn));
    u8 r = (u8)(c->A & v);

    if (r == 0) c->P |= CPU_FLAG_Z;
    else        c->P &= (u8)~CPU_FLAG_Z;

    c->P = (u8)((c->P & ~(CPU_FLAG_N | CPU_FLAG_V)) |
                (v & (CPU_FLAG_N | CPU_FLAG_V)));

    cycles = 4;
} break;  // BIT abs
case 0xC4: {
    u8 v = READ(zp(c,ctx,rfn));
    cmp_u8(c, c->Y, v);
    cycles = 3;
} break;  // CPY zp
case 0x2D:
    c->A &= READ(abs_(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 4;
    break;  // AND abs
case 0x6E: {
    u16 a = abs_(c,ctx,rfn);
    u8 v = READ(a);

    u8 carry_in = (c->P & CPU_FLAG_C) ? 0x80 : 0x00;
    if (v & 0x01) c->P |= CPU_FLAG_C;
    else          c->P &= (u8)~CPU_FLAG_C;

    v = (u8)((v >> 1) | carry_in);
    WRITE(a, v);
    setZN(c, v);

    cycles = 6;
} break;  // ROR abs
case 0x26: {
    u16 a = zp(c,ctx,rfn);
    u8 v = READ(a);

    u8 carry_in = (c->P & CPU_FLAG_C) ? 1 : 0;
    if (v & 0x80) c->P |= CPU_FLAG_C;
    else          c->P &= (u8)~CPU_FLAG_C;

    v = (u8)((v << 1) | carry_in);
    WRITE(a, v);
    setZN(c, v);

    cycles = 5;
} break;  // ROL zp
case 0x45:
    c->A ^= READ(zp(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 3;
    break;  // EOR zp
case 0x46: {
    u16 a = zp(c,ctx,rfn);
    u8 v = READ(a);

    if (v & 0x01) c->P |= CPU_FLAG_C;
    else          c->P &= (u8)~CPU_FLAG_C;

    v = (u8)(v >> 1);
    WRITE(a, v);
    setZN(c, v);

    cycles = 5;
} break;  // LSR zp
case 0x09:
    c->A |= READ(imm(c));
    setZN(c, c->A);
    cycles = 2;
    break;  // ORA #imm

case 0x96:
    WRITE(zpy(c,ctx,rfn), c->X);
    cycles = 4;
    break;  // STX zp,Y
case 0x99:
    WRITE(absy(c,ctx,rfn, NULL), c->A);
    cycles = 5;
    break;  // STA abs,Y
case 0xBA:
    c->X = c->S;
    setZN(c, c->X);
    cycles = 2;
    break;  // TSX
case 0x94:
    WRITE(zpx(c,ctx,rfn), c->Y);
    cycles = 4;
    break;  // STY zp,X
case 0x35:
    c->A &= READ(zpx(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 4;
    break;  // AND zp,X
case 0x11: {
    //bool pcross = false;
    u16 addr = indy(c,ctx,rfn,&pcross);
    c->A |= READ(addr);
    setZN(c, c->A);
    cycles = 5 + pcross;
} break;  // ORA (zp),Y
case 0x01: c->A |= READ(indx(c,ctx,rfn)); setZN(c, c->A); cycles = 6; break;
case 0x0D: c->A |= READ(abs_(c,ctx,rfn)); setZN(c, c->A); cycles = 4; break;
case 0x1D: c->A |= READ(absx(c,ctx,rfn,&pcross)); setZN(c, c->A); cycles = 4 + pcross; break;
case 0x21: c->A &= READ(indx(c,ctx,rfn)); setZN(c, c->A); cycles = 6; break;
case 0x31: c->A &= READ(indy(c,ctx,rfn,&pcross)); setZN(c, c->A); cycles = 5 + pcross; break;
case 0xEE: {
    u16 a = abs_(c,ctx,rfn);
    u8 v = (u8)(READ(a) + 1);
    WRITE(a, v);
    setZN(c, v);
    cycles = 6;
} break;  // INC abs
case 0xD9: {
    //bool pcross = false;
    u16 a = absy(c,ctx,rfn,&pcross);
    u8 v = READ(a);
    u16 diff = (u16)c->A - (u16)v;

    if (c->A >= v) c->P |= CPU_FLAG_C;
    else           c->P &= (u8)~CPU_FLAG_C;

    setZN(c, (u8)diff);
    cycles = 4 + pcross;
} break;  // CMP abs,Y
case 0x19: {
    //bool pcross = false;
    u16 a = absy(c,ctx,rfn,&pcross);
    c->A |= READ(a);
    setZN(c, c->A);
    cycles = 4 + pcross;
} break;  // ORA abs,Y
case 0x15:
    c->A |= READ(zpx(c,ctx,rfn));
    setZN(c, c->A);
    cycles = 4;
    break;  // ORA zp,X
case 0x3D: {
    //bool pcross = false;
    u16 a = absx(c,ctx,rfn,&pcross);
    c->A &= READ(a);
    setZN(c, c->A);
    cycles = 4 + pcross;
} break;  // AND abs,X
case 0xD6: {
    u16 a = zpx(c,ctx,rfn);
    u8 v = (u8)(READ(a) - 1);
    WRITE(a, v);
    setZN(c, v);
    cycles = 6;
} break;  // DEC zp,X
case 0xCE: { u16 a = abs_(c,ctx,rfn); u8 v = (u8)(READ(a) - 1); WRITE(a, v); setZN(c, v); cycles = 6; } break;
case 0xDE: { u16 a = absx(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) - 1); WRITE(a, v); setZN(c, v); cycles = 7; } break;
case 0xF6: {
    u16 a = zpx(c,ctx,rfn);
    u8 v = (u8)(READ(a) + 1);
    WRITE(a, v);
    setZN(c, v);
    cycles = 6;
} break;  // INC zp,X
case 0xFE: { u16 a = absx(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) + 1); WRITE(a, v); setZN(c, v); cycles = 7; } break;
case 0xB4:
    LD(c->Y, READ(zpx(c,ctx,rfn)));
    cycles = 4;
    break;  // LDY zp,X
case 0x39: {
    //bool pcross = false;
    u16 a = absy(c,ctx,rfn,&pcross);
    c->A &= READ(a);
    setZN(c, c->A);
    cycles = 4 + pcross;
} break;  // AND abs,Y

        // --- Undocumented RMW + ALU combos ---
        case 0x03: { u16 a = indx(c,ctx,rfn); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 8; } break; // SLO
        case 0x07: { u16 a = zp(c,ctx,rfn); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 5; } break;
        case 0x0F: { u16 a = abs_(c,ctx,rfn); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 6; } break;
        case 0x13: { u16 a = indy(c,ctx,rfn,NULL); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 8; } break;
        case 0x17: { u16 a = zpx(c,ctx,rfn); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 6; } break;
        case 0x1F: { u16 a = absx(c,ctx,rfn,NULL); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 7; } break;

        case 0x1B: { u16 a = absy(c,ctx,rfn,NULL); u8 v = READ(a); if (v & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; v = (u8)(v << 1); WRITE(a,v); c->A |= v; setZN(c,c->A); cycles = 7; } break;

        case 0x23: { u16 a = indx(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 8; } break; // RLA
        case 0x27: { u16 a = zp(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 5; } break;
        case 0x2F: { u16 a = abs_(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 6; } break;
        case 0x33: { u16 a = indy(c,ctx,rfn,NULL); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 8; } break;
        case 0x37: { u16 a = zpx(c,ctx,rfn); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 6; } break;
        case 0x3F: { u16 a = absx(c,ctx,rfn,NULL); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 7; } break;

        case 0x3B: { u16 a = absy(c,ctx,rfn,NULL); u8 v = rol_u8(c, READ(a)); WRITE(a,v); c->A &= v; setZN(c,c->A); cycles = 7; } break;

        case 0x43: { u16 a = indx(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 8; } break; // SRE
        case 0x47: { u16 a = zp(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 5; } break;
        case 0x4F: { u16 a = abs_(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 6; } break;
        case 0x53: { u16 a = indy(c,ctx,rfn,NULL); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 8; } break;
        case 0x57: { u16 a = zpx(c,ctx,rfn); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 6; } break;
        case 0x5F: { u16 a = absx(c,ctx,rfn,NULL); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 7; } break;

        case 0x5B: { u16 a = absy(c,ctx,rfn,NULL); u8 v = lsr_u8(c, READ(a)); WRITE(a,v); c->A ^= v; setZN(c,c->A); cycles = 7; } break;

        case 0x63: { u16 a = indx(c,ctx,rfn); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 8; } break; // RRA
        case 0x67: { u16 a = zp(c,ctx,rfn); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 5; } break;
        case 0x6F: { u16 a = abs_(c,ctx,rfn); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 6; } break;
        case 0x73: { u16 a = indy(c,ctx,rfn,NULL); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 8; } break;
        case 0x77: { u16 a = zpx(c,ctx,rfn); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 6; } break;
        case 0x7B: { u16 a = absy(c,ctx,rfn,NULL); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 7; } break;
        case 0x7F: { u16 a = absx(c,ctx,rfn,NULL); u8 v = ror_u8(c, READ(a)); WRITE(a,v); adc(c, v); cycles = 7; } break;

        case 0xE3: { u16 a = indx(c,ctx,rfn); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 8; } break; // ISC
        case 0xE7: { u16 a = zp(c,ctx,rfn); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 5; } break;
        case 0xEF: { u16 a = abs_(c,ctx,rfn); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 6; } break;
        case 0xF3: { u16 a = indy(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 8; } break;
        case 0xF7: { u16 a = zpx(c,ctx,rfn); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 6; } break;
        case 0xFB: { u16 a = absy(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 7; } break;
        case 0xFF: { u16 a = absx(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) + 1); WRITE(a, v); sbc(c, v); cycles = 7; } break;

        case 0xC3: { u16 a = indx(c,ctx,rfn); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 8; } break; // DCP
        case 0xC7: { u16 a = zp(c,ctx,rfn); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 5; } break;
        case 0xCF: { u16 a = abs_(c,ctx,rfn); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 6; } break;
        case 0xD3: { u16 a = indy(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 8; } break;
        case 0xD7: { u16 a = zpx(c,ctx,rfn); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 6; } break;
        case 0xDB: { u16 a = absy(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 7; } break;
        case 0xDF: { u16 a = absx(c,ctx,rfn,NULL); u8 v = (u8)(READ(a) - 1); WRITE(a, v); cmp_u8(c, c->A, v); cycles = 7; } break;

        case 0x0B: case 0x2B: c->A &= READ(imm(c)); setZN(c, c->A); if (c->A & 0x80) c->P |= CPU_FLAG_C; else c->P &= (u8)~CPU_FLAG_C; cycles = 2; break; // ANC
        case 0x4B: c->A &= READ(imm(c)); c->A = lsr_u8(c, c->A); cycles = 2; break; // ALR
        case 0x6B: c->A &= READ(imm(c)); c->A = ror_u8(c, c->A); if (((c->A >> 5) ^ (c->A >> 6)) & 1) c->P |= CPU_FLAG_V; else c->P &= (u8)~CPU_FLAG_V; cycles = 2; break; // ARR (approx)
        case 0x8B: c->A = (u8)(c->X & READ(imm(c))); setZN(c, c->A); cycles = 2; break; // XAA (unstable, approximated)
        case 0xAB: c->A = READ(imm(c)); c->X = c->A; setZN(c, c->A); cycles = 2; break; // LAX #imm
        case 0xCB: { u8 v = READ(imm(c)); c->X = (u8)((c->A & c->X) - v); cmp_u8(c, (u8)(c->A & c->X), v); setZN(c, c->X); cycles = 2; } break; // AXS
        case 0xBB: { u8 v = READ(absy(c,ctx,rfn,&pcross)); c->A = (u8)(v & c->S); c->X = c->A; c->S = c->A; setZN(c, c->A); cycles = 4 + pcross; } break; // LAS
        case 0x9B: { u16 a = absy(c,ctx,rfn,NULL); u8 hi = (u8)(((a >> 8) + 1) & 0xFF); u8 v = (u8)(c->A & c->X & hi); c->S = (u8)(c->A & c->X); WRITE(a, v); cycles = 5; } break; // TAS
        case 0x9C: { u16 a = absx(c,ctx,rfn,NULL); u8 hi = (u8)(((a >> 8) + 1) & 0xFF); WRITE(a, (u8)(c->Y & hi)); cycles = 5; } break; // SHY
        case 0x9E: { u16 a = absy(c,ctx,rfn,NULL); u8 hi = (u8)(((a >> 8) + 1) & 0xFF); WRITE(a, (u8)(c->X & hi)); cycles = 5; } break; // SHX
        case 0x9F: { u16 a = absy(c,ctx,rfn,NULL); u8 hi = (u8)(((a >> 8) + 1) & 0xFF); WRITE(a, (u8)(c->A & c->X & hi)); cycles = 5; } break; // AHX abs,Y
        case 0x93: { u16 a = indy(c,ctx,rfn,NULL); u8 hi = (u8)(((a >> 8) + 1) & 0xFF); WRITE(a, (u8)(c->A & c->X & hi)); cycles = 6; } break; // AHX (zp),Y

        case 0xEB: sbc(c, READ(imm(c))); cycles = 2; break; // unofficial SBC immediate


        // --- Illegal/JAM ---
        case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
        case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
            c->jammed = true;
            SDL_Log("KIL/JAM at %04X opcode %02X", (unsigned)(c->PC - 1), op);
            cycles = 1;
            break;

        default:
            c->jammed = true;
            SDL_Log("Unknown opcode at %04X opcode %02X", (unsigned)(c->PC - 1), op);
            cycles = 1;
            break;
    }

    #undef BR
    #undef LD
    #undef READ
    #undef WRITE
    return cycles;
}
