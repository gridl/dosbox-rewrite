#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <algorithm>        // std::min
#include <string>
#include <vector>
#include <map>

using namespace std;

enum opccMode {
    MOD_DECOMPILE=0,
    MOD_EXECUTE
};

enum opccSeg {
    OPSEG_NONE=-1,
    OPSEG_CS=0,
    OPSEG_DS,
    OPSEG_ES,
    OPSEG_FS,
    OPSEG_GS,
    OPSEG_SS
};

const char *opseg2string(const unsigned int x) {
    switch (x) {
        case OPSEG_CS:  return "CS";
        case OPSEG_DS:  return "DS";
        case OPSEG_ES:  return "ES";
        case OPSEG_FS:  return "FS";
        case OPSEG_GS:  return "GS";
        case OPSEG_SS:  return "SS";
    }

    return "";
}

enum opccReg {
    OPREG_NONE=-1,

    // 8-bit
    OPREG_AL=0,
    OPREG_CL,
    OPREG_DL,
    OPREG_BL,
    OPREG_AH,
    OPREG_CH,
    OPREG_DH,
    OPREG_BH,

    // 16-bit
    OPREG_AX=0,
    OPREG_CX,
    OPREG_DX,
    OPREG_BX,
    OPREG_SP,
    OPREG_BP,
    OPREG_SI,
    OPREG_DI,

    // 32-bit
    OPREG_EAX=0,
    OPREG_ECX,
    OPREG_EDX,
    OPREG_EBX,
    OPREG_ESP,
    OPREG_EBP,
    OPREG_ESI,
    OPREG_EDI,

    // segment register
    OPREG_ES=0,
    OPREG_CS,
    OPREG_SS,
    OPREG_DS,
    OPREG_FS,
    OPREG_GS,

    // FPU
    OPDARG_ST0=0
};

enum opccSuffix {
    OPSUFFIX_NONE=0,
    OPSUFFIX_BYTE,
    OPSUFFIX_WORD,
    OPSUFFIX_WORD16,
    OPSUFFIX_WORD32,
    OPSUFFIX_WORD64,
    OPSUFFIX_FLOAT32,       // aka float
    OPSUFFIX_FLOAT64,       // aka double
    OPSUFFIX_FLOAT80,       // aka long double
    OPSUFFIX_FLOATBCD
};

enum opccArgs {
    OPARG_NONE=0,
    OPARG_IB,           // imm8
    OPARG_IBS,          // imm8 sign extended
    OPARG_IW,           // imm<word>
    OPARG_IWS,          // imm<word> sign extended
    OPARG_IW16,         // imm16
    OPARG_IW16S,        // imm16 sign extended
    OPARG_IW32,         // imm32
    OPARG_IW32S,        // imm32 sign extended
    OPARG_IWA           // imm<address size>
};

enum opccDispArg {
    OPDARG_NONE=0,
    OPDARG_rm,          // (rm)
    OPDARG_reg,         // (reg)
    OPDARG_imm,         // (i)
    OPDARG_mem_imm,     // (mem[i])
    OPDARG_immval,      // (some immediate value)
    OPDARG_vexsidx,     // VEX source register index
    OPDARG_xopsidx,     // XOP source register index
    OPDARG_i74,         // bits 7:4 of immediate value
    OPDARG_vm32x,       // (rm) except 32-bit SIB addressing makes index register xmm (SSE) register
    OPDARG_vm32y        // (rm) except 32-bit SIB addressing makes index register ymm (AVX) register
};

enum opccDispArgType {
    OPDARGTYPE_NONE=0,
    OPDARGTYPE_BYTE,        // 8-bit byte
    OPDARGTYPE_WORD,        // 16-bit (if 16-bit code),  32-bit (if 32-bit code)
    OPDARGTYPE_WORD16,      // 16-bit
    OPDARGTYPE_WORD32,      // 32-bit
    OPDARGTYPE_WORD64,      // 64-bit
    OPDARGTYPE_FPU32,       // 32-bit float
    OPDARGTYPE_FPU64,       // 64-bit float
    OPDARGTYPE_FPU80,       // 80-bit float
    OPDARGTYPE_FPUBCD,      // floating point packed BCD
    OPDARGTYPE_FPUENV,      // floating point environment
    OPDARGTYPE_FPUSTATE,    // floating point state
    OPDARGTYPE_FPUREG,      // FPU st(i)
    OPDARGTYPE_SREG,        // segment register (word size)
    OPDARGTYPE_CREG,        // control register (CR0...CR7)
    OPDARGTYPE_DREG,        // debug register (DR0...DR7)
    OPDARGTYPE_TREG,        // test register (TR0...TR7)
    OPDARGTYPE_MMX,         // MMX register/memory 64-bit
    OPDARGTYPE_SSE,         // SSE register/memory 128-bit
    OPDARGTYPE_AVX          // AVX register/memory 256-bit
};

enum {
    // WARNING: this must match the VEX p1-p0 prefix values
    OP_VEX_PREFIX_NONE=0,
    OP_VEX_PREFIX_66,
    OP_VEX_PREFIX_F3,
    OP_VEX_PREFIX_F2,

    OP_VEX_PREFIX_MAX
};

enum {
    // WARNING: this must match the VEX m4-m0 lead byte values
    OP_VEX_LEAD_NONE=0,
    OP_VEX_LEAD_0F,
    OP_VEX_LEAD_0F38,
    OP_VEX_LEAD_0F3A,

    OP_VEX_LEAD_MAX
};

class OpCodeDisplayArg {
public:
    OpCodeDisplayArg() : arg(OPDARG_NONE), argtype(OPDARGTYPE_NONE), argreg(OPREG_NONE), suffix(OPSUFFIX_NONE), ip_relative(false), index(0), immval(0), imm_bitoff(0), imm_bitwidth(0) { }
public:
    enum opccDispArg        arg;
    enum opccDispArgType    argtype;
    enum opccReg            argreg;
    enum opccSuffix         suffix;
    bool                    ip_relative; // NTS: relative to the value of IP after decoding instruction
    uint8_t                 index;
    uint64_t                immval;
    uint8_t                 imm_bitoff,imm_bitwidth; // if the immediate value is only some of the bits
public:
    inline bool operator==(const OpCodeDisplayArg &r) const {
        if (arg != r.arg) return false;
        if (argtype != r.argtype) return false;
        if (argreg != r.argreg) return false;
        if (suffix != r.suffix) return false;
        if (index != r.index) return false;
        if (immval != r.immval) return false;
        return true;
    }
};

const char *suffix_str(enum opccSuffix c,const unsigned int opsize) {
    switch (c) {
        case OPSUFFIX_BYTE:
            return "b";
        case OPSUFFIX_WORD:
            if (opsize >= 32) return "d";
            else return "w";
        case OPSUFFIX_WORD16:
            return "w";
        case OPSUFFIX_WORD32:
            return "d";
        case OPSUFFIX_WORD64:
            return "q";
        case OPSUFFIX_FLOAT32:
            return "f32";
        case OPSUFFIX_FLOAT64:
            return "f64";
        case OPSUFFIX_FLOAT80:
            return "f80";
        case OPSUFFIX_FLOATBCD:
            return "fbcd";
        default:
            break;
    }

    return "";
}

FILE*                   in_fp = NULL;
string                  in_path;

FILE*                   out_fp = NULL;
string                  out_path;

string                  symbol_path;

map<string,bool>        opcode_symbol_list;

enum opccMode           cc_mode = MOD_DECOMPILE;

bool                    mprefix_exists = false;

int                     sel_code_width = 16;
int                     sel_addr_width = 16;

bool                    generic_case_1632 = false;
bool                    generic1632 = false;
bool                    c32vars = false;
 
static void help() {
    fprintf(stderr,"opcc [options]\n");
    fprintf(stderr,"Take a DOSBox-XR opcode sheet and generate C++ code to handle it.\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"   -i <input>          Input file path or - for stdin\n");
    fprintf(stderr,"   -o <output>         Output file path or - for stdout\n");
    fprintf(stderr,"   -s <output>         Output file for symbols (enum)\n");
    fprintf(stderr,"   -m <mode>           Generation mode:\n");
    fprintf(stderr,"        decompile        Generate code to decompile/disassemble\n");
    fprintf(stderr,"        execute          Generate code to execute instructions\n");
    fprintf(stderr,"   -c <width>          Code width (16 or 32)\n");
    fprintf(stderr,"   -a <width>          Address width (16 or 32)\n");
    fprintf(stderr,"   -g                  Generic 16/32-bit decode\n");
    fprintf(stderr,"   -c32vars            Variables code32/addr32 exist\n");
    fprintf(stderr,"   -gc1632             code16/addr32 and code32/addr16 are generic case\n");
}

static bool parse(int argc,char **argv) {
    char *a;
    int i;

    for (i=1;i < argc;) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return false;
            }
            else if (!strcmp(a,"g")) {
                generic1632 = true;
            }
            else if (!strcmp(a,"gc1632")) {
                generic_case_1632 = true;
            }
            else if (!strcmp(a,"c32vars")) {
                c32vars = true;
            }
            else if (!strcmp(a,"c")) {
                a = argv[i++];
                if (a == NULL) return false;

                sel_code_width = atoi(a);
                if (!(sel_code_width == 16 || sel_code_width == 32)) return false;
            }
            else if (!strcmp(a,"a")) {
                a = argv[i++];
                if (a == NULL) return false;

                sel_addr_width = atoi(a);
                if (!(sel_addr_width == 16 || sel_addr_width == 32)) return false;
            }
            else if (!strcmp(a,"i")) {
                a = argv[i++];
                if (a == NULL) return false;
                in_path = a;
            }
            else if (!strcmp(a,"o")) {
                a = argv[i++];
                if (a == NULL) return false;
                out_path = a;
            }
            else if (!strcmp(a,"s")) {
                a = argv[i++];
                if (a == NULL) return false;
                symbol_path = a;
            }
            else if (!strcmp(a,"m")) {
                a = argv[i++];

                if (!strcmp(a,"decompile"))
                    cc_mode = MOD_DECOMPILE;
                else if (!strcmp(a,"execute"))
                    cc_mode = MOD_EXECUTE;
                else {
                    fprintf(stderr,"Unknown mode\n");
                    return false;
                }
            }
            else {
                fprintf(stderr,"Unexpected switch %s\n",a);
                return false;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg %s\n",a);
            return false;
        }
    }

    if (in_path.empty() || out_path.empty()) {
        fprintf(stderr,"Must specify input and output file. See -h option for help\n");
        return false;
    }

    return true;
}

class OpByte {
public:
    OpByte() : isprefix(false), segoverride(OPSEG_NONE), modregrm(false), reg_from_opcode(false),
        already(false), addrsz32(false), opsz32(false), suffix(OPSUFFIX_NONE), opmap_valid(false),
        mprefix(0), mprefix_exists_here(false), amd3dnowsuffix(-1), amd3dnow_here(false), final(false), illegal(false),
        vex(false), vexsize(0), vexlead(OP_VEX_LEAD_NONE), vexsidx(-1), vw(0), vexprefix(OP_VEX_PREFIX_NONE),
        vex_decompose_here(0), xop(false), xopmap(0), xopw(0), xopsize(0), xoppp(0), xopsidx(-1),
        xop_decompose_here(0) {
    }
    ~OpByte() {
        free_opmap();
    }
    void free_opmap(void) {
        for (size_t i=0;i < 256;i++) free_opmap_code(i);
    }
    void free_opmap_code(size_t i) {
        if (opmap_valid) {
            if (opmap[i] != NULL) {
                delete opmap[i];
                opmap[i] = NULL;
            }
        }
    }
    void init_opmap(void) {
        if (!opmap_valid) {
            opmap_valid = true;
            for (size_t i=0;i < 256;i++) opmap[i] = NULL;
        }
    }
    bool init_opcode(const uint8_t op) {
        if (!opmap_valid)
            init_opmap();

        if (opmap[op] == NULL) {
            opmap[op] = new OpByte();
            return true;
        }

        return false;
    }
    bool exist_opcode(const uint8_t op) {
        if (!opmap_valid) return false;
        return (opmap[op] != NULL)?true:false;
    }
public:
    inline bool operator==(const OpByte &r) const {
        if (format_str != r.format_str) return false;
        if (name != r.name) return false;
        if (segoverride != r.segoverride) return false;
        if (isprefix != r.isprefix) return false;
        if (modregrm != r.modregrm) return false;
        if (reg_from_opcode != r.reg_from_opcode) return false;
        if (suffix != r.suffix) return false;
        if (immarg != r.immarg) return false;
        if (disparg != r.disparg) return false;
        if (vex_decompose_here != r.vex_decompose_here) return false;
        if (xop_decompose_here != r.xop_decompose_here) return false;
        if (mprefix_exists_here != r.mprefix_exists_here) return false;
        if (amd3dnowsuffix != r.amd3dnowsuffix) return false;
        if (amd3dnow_here != r.amd3dnow_here) return false;
        if (opmap_valid != r.opmap_valid) return false;
        if (vexprefix != r.vexprefix) return false;
        if (exec_code != r.exec_code) return false;
        if (addrsz32 != r.addrsz32) return false;
        if (vexsidx != r.vexsidx) return false;
        if (xopsidx != r.xopsidx) return false;
        if (mprefix != r.mprefix) return false;
        if (illegal != r.illegal) return false;
        if (vexlead != r.vexlead) return false;
        if (vexsize != r.vexsize) return false;
        if (xopmap != r.xopmap) return false;
        if (opsz32 != r.opsz32) return false;
        if (xopw != r.xopw) return false;
        if (vex != r.vex) return false;
        if (xop != r.xop) return false;
        if (vw != r.vw) return false;

        if (opmap_valid) {
            assert(r.opmap_valid);
            for (unsigned int i=0;i < 256;i++) {
                OpByte *a = opmap[i];
                OpByte *b = r.opmap[i];

                if ((a != NULL) != (b != NULL))
                    return false;

                if (a != NULL) {
                    assert(b != NULL);
                    if (!(*a == *b))
                        return false;
                }
            }
        }

        return true;
    }
public:
    vector<string>  exec_code;  // executable code (pre-filtering)
    // opbyte [mod/reg/rm [sib] [disp]] [immediate]
    string          format_str;
    string          name;       // JMP, etc.
    string          opspec;
    opccSeg         segoverride;// segment override
    bool            isprefix;   // opcode is prefix
    bool            modregrm;   // a MOD/REG/RM byte follows the opcode.
                                // if more == false, this byte is final and mod/reg/rm decode the instruction as normal.
                                // if more == true, this byte is NOT final and the next byte is the mod/reg/rm byte where REG determines the opcode.
                                // In some cases (FPU 8087 opcodes) mod/reg/rm both determines the memory address or FPU register and in other cases
                                // the mod/reg/rm byte IS the second byte of the opcode (when mod == 3).
    bool            reg_from_opcode; // reg from opcode, reg = (opcode >> 3) & 7
    bool            already;
    bool            addrsz32;
    bool            opsz32;
    enum opccSuffix suffix;     // suffix to name
    vector<enum opccArgs> immarg;
    vector<OpCodeDisplayArg> disparg;
    uint8_t         mprefix;    // mandatory prefix
    int             amd3dnowsuffix; // AMD 3DNow! suffix byte (after mod/reg/rm)
    bool            mprefix_exists_here;
    bool            amd3dnow_here;
    bool            illegal;
    bool            final;
    bool            vex;
    bool            xop;
    bool            vw;
    bool            xopw;
    uint8_t         xoppp;
    uint8_t         xopmap;
    unsigned int    xopsize;
    unsigned int    vexsize;
    uint8_t         vexlead;
    int             vexsidx;
    int             xopsidx;
    int             vexprefix;
    uint8_t         vex_decompose_here;
    uint8_t         xop_decompose_here;
public:
    OpByte*         opmap[256];
    bool            opmap_valid;
};

vector<OpByte*>     last_opcodes;
OpByte              opmap16,opmap32;

// C/C++ implementation of Perl's chomp function.
// static copy to differentiate from target system vs build system.
static void opcc_chomp(char *s) {
    char *t = s + strlen(s) - 1;
    while (t >= s && (*t == '\n' || *t == '\r')) *t-- = 0;
}

class parse_opcode_state {
public:
    parse_opcode_state() {
        vw = 0;
        xopsidx = -1;
        vexsidx = -1;
        oprange_min = -1;
        oprange_max = -1; // last byte of opcode takes the range (min,max) inclusive
        mrm_reg_match = -1; // /X style, to say that opcode is specified by REG field of mod/reg/rm
        reg_from_opcode = false; // if set, lowest 3 bits of opcode define REG field
        vexprefix = OP_VEX_PREFIX_NONE;
        vexlead = OP_VEX_LEAD_NONE;
        segoverride = OPSEG_NONE;
        suffix = OPSUFFIX_NONE;
        amd3dnowsuffix = -1;
        is_prefix = false; // opcode is prefix, changes decode state then starts another opcode
        mod_not_3 = false;
        mod_is_3 = false;
        modregrm = false;
        addrsz32 = false;
        illegal = false;
        opsz32 = false;
        xopsize = 0;
        vexsize = 0;
        mprefix = 0;
        vex = false;
        xop = false;
        xopmap = 0;
        xoppp = 0;
        xopw = 0;
        op = 0;
    }
public:
    bool                    reg_from_opcode; // if set, lowest 3 bits of opcode define REG field
    bool                    is_prefix; // opcode is prefix, changes decode state then starts another opcode
    bool                    modregrm;
    bool                    mod_not_3;      // some opcodes are not valid if mod != 3
    bool                    mod_is_3;
    bool                    addrsz32;
    bool                    illegal;
    bool                    opsz32;
    bool                    xopw;
    bool                    vex;
    bool                    xop;
    uint8_t                 xoppp;
    uint8_t                 xopmap;
    unsigned int            xopsize;
    unsigned int            vexsize;
    uint8_t                 vexlead;
    int                     oprange_min;
    int                     oprange_max; // last byte of opcode takes the range (min,max) inclusive
    int                     mrm_reg_match; // /X style, to say that opcode is specified by REG field of mod/reg/rm
    enum opccSeg            segoverride;
    enum opccSuffix         suffix;     // suffix to name
    uint8_t                 mprefix;    // mandatory prefix
    int                     amd3dnowsuffix; // AMD 3DNow! suffix byte (after mod/reg/rm)
    int                     vexprefix;
    int                     vexsidx;
    int                     xopsidx;
    bool                    vw;
public:
    string                  format_str;
    string                  name;
    string                  opspec;
    vector<enum opccArgs>   immarg;
    vector<OpCodeDisplayArg> disparg;
    unsigned char           ops[16];
    uint8_t                 op;
};

bool parse_opcode_def_gen1(parse_opcode_state &st,OpByte& maproot) {
    OpByte *map = &maproot,*submap;

    if (st.mprefix != 0) {
        if (!map->exist_opcode(st.mprefix)) {
            fprintf(stderr,"Mandatory prefix refers to opcode that doesn't exist\n");
            return false;
        }
        map = map->opmap[st.mprefix];
        assert(map != NULL);
        if (!map->isprefix) {
            fprintf(stderr,"Mandatory prefix refers to opcode that isn't a prefix\n");
            return false;
        }
        map->mprefix_exists_here = true;
    }

    for (unsigned int i=0;i < st.op;i++) {
        const uint8_t opcode = st.ops[i];
        map->init_opcode(opcode);
        map = map->opmap[opcode];
        assert(map != NULL);
        if (map->final) {
            fprintf(stderr,"Opcode overlaps another opcode, mid-sequence (at byte %u = 0x%02x opcode '%s')\n",
                i,opcode,map->name.c_str());
            return false;
        }

        if (st.vex) {
            if (st.ops[0] == 0xC4 && i == 2)
                map->vex_decompose_here = 3; // 3-byte form
            else if (st.ops[0] == 0xC5 && i == 1)
                map->vex_decompose_here = 2; // 2-byte form
        }
        else if (st.xop) {
            if (st.ops[0] == 0x8F && i == 2)
                map->xop_decompose_here = 3;
        }
    }

    if (st.modregrm) {
        map->modregrm = st.modregrm;
        if (st.mrm_reg_match >= 0) {
            if (st.amd3dnowsuffix >= 0) {
                // correct me if there are any instructions that differ by mod byte or match by reg field
                fprintf(stderr,"AMD 3DNow! opcode encoding requires unconditional mod/reg/rm field\n");
                return false;
            }
            if (map->final) {
                fprintf(stderr,"Opcode with mod/reg/rm overlaps another opcode without mod/reg/rm (opcode '%s')\n",map->name.c_str());
                return false;
            }

            for (unsigned int mod=0;mod < 4;mod++) {
                for (unsigned int rm=0;rm < 8;rm++) {
                    const uint8_t mrm = (mod << 6) + (st.mrm_reg_match << 3) + rm;

                    if (mod == 3 && st.mod_not_3)
                        continue;
                    else if (mod != 3 && st.mod_is_3)
                        continue;

                    map->init_opcode(mrm);
                    submap = map->opmap[mrm];
                    assert(submap != NULL);

                    if (submap->final) {
                        fprintf(stderr,"Opcode with mod/reg/rm overlaps another opcode with mod/reg/rm (mod=%u reg=%u rm=%u opcode='%s')\n",
                            mod,st.mrm_reg_match,rm,submap->name.c_str());
                        return false;
                    }

                    last_opcodes.push_back(submap);
                    submap->modregrm = false;
                    submap->disparg = st.disparg;
                    submap->format_str = st.format_str;
                    submap->segoverride = st.segoverride;
                    submap->amd3dnowsuffix = st.amd3dnowsuffix;
                    submap->vexprefix = st.vexprefix;
                    submap->isprefix = st.is_prefix;
                    submap->opspec = st.opspec;
                    submap->immarg = st.immarg;
                    submap->mprefix = st.mprefix;
                    submap->suffix = st.suffix;
                    submap->name = st.name;
                    submap->opsz32 = st.opsz32;
                    submap->addrsz32 = st.addrsz32;
                    submap->illegal = st.illegal;
                    submap->vexsize = st.vexsize;
                    submap->vexlead = st.vexlead;
                    submap->vexsidx = st.vexsidx;
                    submap->xopsidx = st.xopsidx;
                    submap->xopsize = st.xopsize;
                    submap->xopmap = st.xopmap;
                    submap->xoppp = st.xoppp;
                    submap->xopw = st.xopw;
                    submap->final = true;
                    submap->vex = st.vex;
                    submap->xop = st.xop;
                    submap->vw = st.vw;
                }
            }
        }
        else if (st.mod_not_3 || st.mod_is_3) {
            if (st.amd3dnowsuffix >= 0) {
                // correct me if there are any instructions that differ by mod byte or match by reg field
                fprintf(stderr,"AMD 3DNow! opcode encoding requires unconditional mod/reg/rm field\n");
                return false;
            }

            for (unsigned int mod=0;mod < 4;mod++) {
                for (unsigned int reg=0;reg < 8;reg++) {
                    for (unsigned int rm=0;rm < 8;rm++) {
                        const uint8_t mrm = (mod << 6) + (reg << 3) + rm;

                        if (mod == 3 && st.mod_not_3)
                            continue;
                        else if (mod != 3 && st.mod_is_3)
                            continue;

                        map->init_opcode(mrm);
                        submap = map->opmap[mrm];
                        assert(submap != NULL);

                        if (submap->final) {
                            fprintf(stderr,"Opcode with mod/reg/rm overlaps another opcode with mod/reg/rm (mod=%u reg=%u rm=%u opcode='%s')\n",
                                mod,reg,rm,submap->name.c_str());
                            return false;
                        }

                        last_opcodes.push_back(submap);
                        submap->modregrm = false;
                        submap->disparg = st.disparg;
                        submap->format_str = st.format_str;
                        submap->segoverride = st.segoverride;
                        submap->amd3dnowsuffix = st.amd3dnowsuffix;
                        submap->vexprefix = st.vexprefix;
                        submap->isprefix = st.is_prefix;
                        submap->opspec = st.opspec;
                        submap->immarg = st.immarg;
                        submap->mprefix = st.mprefix;
                        submap->suffix = st.suffix;
                        submap->name = st.name;
                        submap->opsz32 = st.opsz32;
                        submap->addrsz32 = st.addrsz32;
                        submap->illegal = st.illegal;
                        submap->vexsize = st.vexsize;
                        submap->vexlead = st.vexlead;
                        submap->vexsidx = st.vexsidx;
                        submap->xopsidx = st.xopsidx;
                        submap->xopsize = st.xopsize;
                        submap->xopmap = st.xopmap;
                        submap->xoppp = st.xoppp;
                        submap->xopw = st.xopw;
                        submap->final = true;
                        submap->vex = st.vex;
                        submap->xop = st.xop;
                        submap->vw = st.vw;
                    }
                }
            }
        }
        else {
            if (map->final) {
                fprintf(stderr,"Opcode overlaps another opcode\n");
                return false;
            }

            if (st.amd3dnowsuffix >= 0) {
                map->modregrm = false;
                map->amd3dnow_here = true;
                map->init_opcode(st.amd3dnowsuffix);
                map = map->opmap[st.amd3dnowsuffix];
                assert(map != NULL);

                if (map->final) {
                    fprintf(stderr,"Opcode overlaps another opcode\n");
                    return false;
                }
            }

            if (map->amd3dnow_here) {
                fprintf(stderr,"Opcode overlaps another opcode that defines as 3DNow! instruction\n");
                return false;
            }

            last_opcodes.push_back(map);
            map->illegal = st.illegal;
            map->opsz32 = st.opsz32;
            map->addrsz32 = st.addrsz32;
            map->segoverride = st.segoverride;
            map->amd3dnowsuffix = st.amd3dnowsuffix;
            map->format_str = st.format_str;
            map->vexprefix = st.vexprefix;
            map->isprefix = st.is_prefix;
            map->disparg = st.disparg;
            map->opspec = st.opspec;
            map->immarg = st.immarg;
            map->mprefix = st.mprefix;
            map->vexsize = st.vexsize;
            map->vexlead = st.vexlead;
            map->vexsidx = st.vexsidx;
            map->xopsidx = st.xopsidx;
            map->xopsize = st.xopsize;
            map->suffix = st.suffix;
            map->xopmap = st.xopmap;
            map->xoppp = st.xoppp;
            map->xopw = st.xopw;
            map->name = st.name;
            map->final = true;
            map->vex = st.vex;
            map->xop = st.xop;
            map->vw = st.vw;
        }
    }
    else {
        if (map->final) {
            fprintf(stderr,"Opcode overlaps another opcode\n");
            return false;
        }

        last_opcodes.push_back(map);
        map->illegal = st.illegal;
        map->opsz32 = st.opsz32;
        map->addrsz32 = st.addrsz32;
        map->reg_from_opcode = st.reg_from_opcode;
        map->amd3dnowsuffix = st.amd3dnowsuffix;
        map->segoverride = st.segoverride;
        map->format_str = st.format_str;
        map->vexprefix = st.vexprefix;
        map->isprefix = st.is_prefix;
        map->disparg = st.disparg;
        map->opspec = st.opspec;
        map->immarg = st.immarg;
        map->vexsize = st.vexsize;
        map->vexlead = st.vexlead;
        map->vexsidx = st.vexsidx;
        map->xopsidx = st.xopsidx;
        map->mprefix = st.mprefix;
        map->xopsize = st.xopsize;
        map->suffix = st.suffix;
        map->xopmap = st.xopmap;
        map->xoppp = st.xoppp;
        map->xopw = st.xopw;
        map->name = st.name;
        map->final = true;
        map->vex = st.vex;
        map->xop = st.xop;
        map->vw = st.vw;

        if (st.amd3dnowsuffix >= 0) {
            fprintf(stderr,"AMD 3DNow! opcode encoding requires mod/reg/rm field\n");
            return false;
        }
    }

    return true;
}

bool parse_opcode_def(char *line,unsigned long lineno,char *s) {
    parse_opcode_state st;
    bool allow_modregrm = true;
    bool allow_op = true; // once set to false, can't define more opcode bytes
    char *next = NULL;
    int argc = 0;

    next = strchr(s,':');
    if (next != NULL) *next++ = 0;

    st.opspec = s;
    while (*s != 0) {
        if (isblank(*s)) {
            s++;
            continue;
        }

        // ASCII chop next space
        char *ns = strchr(s,' ');
        if (ns != NULL) *ns++ = 0;

        if (!strncmp(s,"0x",2)) { // opcode byte
            if (!allow_op) {
                fprintf(stderr,"Opcode byte not allowed at this point\n");
                return false;
            }

            /* use long parsing to catch overlarge values */
            unsigned long b = strtoul(s,&s,0);

            if (b > 0xFFUL) {
                fprintf(stderr,"Opcode byte out of range\n");
                return false;
            }
            else if (*s != 0 && *s != ':' && !isblank(*s)) {
                fprintf(stderr,"Excess char follows opcode byte: %s\n",s);
                return false;
            }
            else if ((st.op+4) >= sizeof(st.ops)) {
                fprintf(stderr,"Opcode byte string too long\n");
                return false;
            }

            st.ops[st.op++] = (unsigned char)b;
        }
        else if (!strncmp(s,"amd3dnow(",9)) {
            if (st.amd3dnowsuffix >= 0) {
                fprintf(stderr,"AMD 3DNow! suffix can only be defined once\n");
                return false;
            }

            s += 9;
            st.amd3dnowsuffix = (int)strtoul(s,&s,0);
            if (st.amd3dnowsuffix > 255) {
                fprintf(stderr,"AMD 3DNow! suffix out of range\n");
                return false;
            }
        }
        else if (!strncmp(s,"mprefix(",8)) {
            if (st.mprefix != 0) {
                fprintf(stderr,"Mandatory prefix already defined\n");
                return false;
            }

            s += 8;
            st.mprefix = (int)strtoul(s,&s,0);
            mprefix_exists = true;

            if (st.mprefix == 0) {
                fprintf(stderr,"Invalid mandatory prefix mprefix()\n");
                return false;
            }
        }
        else if (!strcmp(s,"mod/reg/rm")) {
            if (st.modregrm) {
                fprintf(stderr,"Cannot define mod/reg/rm field twice\n");
                return false;
            }
            else if (!allow_modregrm) {
                fprintf(stderr,"Cannot define mod/reg/rm field here\n");
                return false;
            }

            allow_op = false;
            st.modregrm = true;
        }
        else if (s[0] == '/' && isdigit(s[1])) { // /1, /3, etc. to match by REG value
            if (!st.modregrm) {
                fprintf(stderr,"Cannot use REG matching (/1 /3 etc) without mod/reg/rm\n");
                return false;
            }
            else if (st.mrm_reg_match >= 0) {
                fprintf(stderr,"Cannot match more than one REG value\n");
                return false;
            }

            /* use long parsing to catch overlarge values */
            unsigned long b = strtoul(s+1,&s,0);

            if (b > 7) {
                fprintf(stderr,"/REG match out of range\n");
                return false;
            }

            st.mrm_reg_match = b;
        }
        else if (!strncmp(s,"vw(",3)) {
            s += 3;

            st.vw = (int)strtol(s,&s,0);
            if (st.vw < 0 || st.vw > 1) {
                fprintf(stderr,"vex W bit must be 0 or 1\n");
                return false;
            }
        }
        else if (!strncmp(s,"xopsidx(",8)) {
            s += 8;

            st.xopsidx = (int)strtol(s,&s,0);
            if (st.xopsidx < -1 || st.xopsidx > 7) {
                fprintf(stderr,"xop source register index must be 0..7, or -1 to indicate no match\n");
                return false;
            }
        }
        else if (!strncmp(s,"vsidx(",6)) {
            s += 6;

            st.vexsidx = (int)strtol(s,&s,0);
            if (st.vexsidx < -1 || st.vexsidx > 7) {
                fprintf(stderr,"vex source register index must be 0..7, or -1 to indicate no match\n");
                return false;
            }
        }
        else if (!strncmp(s,"vsize(",6)) {
            s += 6;

            st.vexsize = (int)strtoul(s,&s,0);
            if (!(st.vexsize == 128 || st.vexsize == 256)) {
                fprintf(stderr,"vex size must be 128, 256\n");
                return false;
            }
        }
        else if (!strncmp(s,"xoppp(",6)) {
            s += 6;

            st.xoppp = (int)strtoul(s,&s,0);
            if (st.xoppp > 3) {
                fprintf(stderr,"xoppp out of range\n");
                return false;
            }
        }
        else if (!strncmp(s,"xopsize(",8)) {
            s += 8;

            st.xopsize = (int)strtoul(s,&s,0);
            if (!(st.xopsize == 128 || st.xopsize == 256)) {
                fprintf(stderr,"xopsize must be 128 or 256\n");
                return false;
            }
        }
        else if (!strncmp(s,"xopw(",5)) {
            s += 5;

            st.xopw = (int)strtoul(s,&s,0) > 0;
        }
        else if (!strncmp(s,"xopmap(",7)) {
            s += 7;

            st.xopmap = (int)strtoul(s,&s,0);
            if (st.xopmap > 31) {
                fprintf(stderr,"invalid xopmap value\n");
                return false;
            }
        }
        else if (!strncmp(s,"vprefix(",8)) {
            s += 8;

            {
                /* eat ) */
                char *e = s + strlen(s) - 1;
                if (e >= s && *e == ')') *e = 0;
            }

            if (!strcmp(s,"none")) {
                st.vexprefix = OP_VEX_PREFIX_NONE;
            }
            else if (!strcmp(s,"0x66")) {
                st.vexprefix = OP_VEX_PREFIX_66;
            }
            else if (!strcmp(s,"0xF3")) {
                st.vexprefix = OP_VEX_PREFIX_F3;
            }
            else if (!strcmp(s,"0xF2")) {
                st.vexprefix = OP_VEX_PREFIX_F2;
            }
            else {
                fprintf(stderr,"vex prefix must be none, 0x66, 0xF3, or 0xF2\n");
                return false;
            }
        }
        else if (!strncmp(s,"vlead(",6)) {
            s += 6;

            {
                /* eat ) */
                char *e = s + strlen(s) - 1;
                if (e >= s && *e == ')') *e = 0;
            }

            if (!strcmp(s,"0x0F")) {
                st.vexlead = OP_VEX_LEAD_0F;
            }
            else if (!strcmp(s,"0x0F,0x38")) {
                st.vexlead = OP_VEX_LEAD_0F38;
            }
            else if (!strcmp(s,"0x0F,0x3A")) {
                st.vexlead = OP_VEX_LEAD_0F3A;
            }
            else {
                fprintf(stderr,"vex lead must be 0x0F, 0x0F,0x38, or 0x0F,0x3A\n");
                return false;
            }
        }
        else if (!strcmp(s,"xop")) {
            if (st.op != 0) {
                fprintf(stderr,"xop declaration must appear first before opcode bytes\n");
                return false;
            }
            else if (st.xop) {
                fprintf(stderr,"xop declaration specified more than once\n");
                return false;
            }

            st.xop = true;
        }
        else if (!strcmp(s,"vex")) {
            if (st.op != 0) {
                fprintf(stderr,"vex declaration must appear first before opcode bytes\n");
                return false;
            }
            else if (st.vex) {
                fprintf(stderr,"vex declaration specified more than once\n");
                return false;
            }

            st.vex = true;
        }
        else if (!strcmp(s,"ib")) {
            st.immarg.push_back(OPARG_IB);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"ibs")) {
            st.immarg.push_back(OPARG_IBS);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iwa")) {
            st.immarg.push_back(OPARG_IWA);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iw")) {
            st.immarg.push_back(OPARG_IW);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iws")) {
            st.immarg.push_back(OPARG_IWS);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iw16")) {
            st.immarg.push_back(OPARG_IW16);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iw16s")) {
            st.immarg.push_back(OPARG_IW16S);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iw32")) {
            st.immarg.push_back(OPARG_IW32);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"iw32s")) {
            st.immarg.push_back(OPARG_IW32S);
            allow_modregrm = false;
            allow_op = false;
        }
        else if (!strcmp(s,"prefix")) {
            st.is_prefix = true;
        }
        else if (!strcmp(s,"mod!=3")) {
            st.mod_not_3 = true;
        }
        else if (!strcmp(s,"mod==3")) {
            st.mod_is_3 = true;
        }
        else if (!strcmp(s,"addrsz32")) {
            st.addrsz32 = true;
        }
        else if (!strcmp(s,"opsz32")) {
            st.opsz32 = true;
        }
        else if (!strncmp(s,"segoverride(",12)) {
            s += 12;
            while (isblank(*s)) s++;

            if (st.segoverride != OPSEG_NONE) {
                fprintf(stderr,"You already declared a segment override\n");
                return false;
            }
            else if (!st.is_prefix) {
                fprintf(stderr,"Segment override must be declared a prefix\n");
                return false;
            }

            if (!strncmp(s,"cs",2))
                st.segoverride = OPSEG_CS;
            else if (!strncmp(s,"ds",2))
                st.segoverride = OPSEG_DS;
            else if (!strncmp(s,"es",2))
                st.segoverride = OPSEG_ES;
            else if (!strncmp(s,"fs",2))
                st.segoverride = OPSEG_FS;
            else if (!strncmp(s,"gs",2))
                st.segoverride = OPSEG_GS;
            else if (!strncmp(s,"ss",2))
                st.segoverride = OPSEG_SS;
            else {
                fprintf(stderr,"Unknown segment override\n");
                return false;
            }
        }
        else if (!strncmp(s,"range(",6)) {
            if (st.oprange_min >= 0 || st.oprange_max >= 0) {
                fprintf(stderr,"Cannot define more than one opcode range\n");
                return false;
            }
            else if (!allow_op) {
                fprintf(stderr,"Opcode range not allowed here\n");
                return false;
            }

            s += 6;
            while (isblank(*s)) s++;
            st.oprange_min = strtol(s,&s,0);
            while (isblank(*s)) s++;
            if (*s != ',') {
                fprintf(stderr,"Syntax error in range()\n");
                return false;
            }
            s++;
            while (isblank(*s)) s++;
            st.oprange_max = strtol(s,&s,0);
            while (isblank(*s)) s++;
            if (*s != ')') {
                fprintf(stderr,"Syntax error in range(), no closing parenthesis\n");
                return false;
            }

            if (st.oprange_min < 0 || st.oprange_min > 0xFF || st.oprange_max < 0 || st.oprange_max > 0xFF || st.oprange_min > st.oprange_max) {
                fprintf(stderr,"Opcode range error. min=%d max=%d\n",st.oprange_min,st.oprange_max);
                return false;
            }

            allow_op = false;
        }
        else if (!strcmp(s,"reg=op02")) {
            if (st.modregrm) {
                fprintf(stderr,"Cannot declare reg = opcode[0:2] when reg already comes from mod/reg/rm\n");
                return false;
            }

            st.reg_from_opcode = true;
        }
        else {
            fprintf(stderr,"Syntax error: %s\n",s);
            return false;
        }

        if (ns != NULL) s = ns;
        else break;
    }

    if (st.op == 0 && st.oprange_min < 0) {
        fprintf(stderr,"Opcode bytes not defined\n");
        return false;
    }

    if (next == NULL) {
        fprintf(stderr,"Opcode does not define anything\n");
        return false;
    }
    while (isblank(*next)) next++;

    // mandatory prefixes may only be 0x66, 0xF2, or 0xF3
    if (!(st.mprefix == 0x00 || st.mprefix == 0x66 || st.mprefix == 0xF2 || st.mprefix == 0xF3)) {
        fprintf(stderr,"Invalid mandatory prefix. Must be 0x66, 0xF2, or 0xF3\n");
        return false;
    }

    // AMD 3Dnow! suffixes are only valid if the opcode is 0x0F 0x0F mod/reg/rm
    if (st.amd3dnowsuffix >= 0) {
        if (st.op < 2) {
            fprintf(stderr,"AMD 3Dnow! opcodes need to be at least 2 opcodes\n");
            return false;
        }
        if (memcmp(st.ops,"\x0F\x0F",2)) {
            fprintf(stderr,"AMD 3Dnow! opcodes must start with 0x0F 0x0F\n");
            return false;
        }
        if (!st.modregrm) {
            fprintf(stderr,"AMD 3Dnow! opcodes must start with 0x0F 0x0F mod/reg/rm\n");
            return false;
        }
    }

    // the common pattern in Intel opcodes seems to be that if a mandatory prefix
    // is present, it's followed by 0x0F. so enforce that. lift this restriction
    // later if that turns out to be false.
    //
    // UPDATE: Guess which instruction violates this rule? The "PAUSE" instruction!
    //         Now we allow either 0x0F or 0x90.
    if (st.mprefix != 0) {
        if (st.op == 0) {
            fprintf(stderr,"Mandatory prefix requires static opcode (range not allowed)\n");
            return false;
        }
        else if (st.ops[0] != 0x0F && st.ops[0] != 0x90) {
            fprintf(stderr,"Mandatory prefix requires first opcode byte to be 0x0F or 0x90\n");
            return false;
        }
    }

    argc = 0;
    s = next;
    while (*s != 0) {
        if (isblank(*s)) {
            s++;
            continue;
        }

        // ASCII chop next space
        char *ns = strchr(s,' ');
        if (ns != NULL) *ns++ = 0;

        if (argc == 0) { // "NAME"b
            if (!strcmp(s,"illegal")) {
                st.illegal = true;
            }
            else {
                if (!st.name.empty()) {
                    fprintf(stderr,"Syntax error: %s\n",s);
                    return false;
                }

                if (*s != '\"') {
                    fprintf(stderr,"Name must be kept in quotes\n");
                    return false;
                }
                s++;
                while (*s != 0 && *s != '\"') st.name += *s++;
                if (*s != '\"') {
                    fprintf(stderr,"Name must be kept in quotes\n");
                    return false;
                }
                s++;

                if (!strcmp(s,"b"))
                    st.suffix = OPSUFFIX_BYTE;
                else if (!strcmp(s,"w"))
                    st.suffix = OPSUFFIX_WORD;
                else if (!strcmp(s,"w16"))
                    st.suffix = OPSUFFIX_WORD16;
                else if (!strcmp(s,"w32"))
                    st.suffix = OPSUFFIX_WORD32;
                else if (!strcmp(s,"w64"))
                    st.suffix = OPSUFFIX_WORD64;
                else if (!strcmp(s,"f32"))
                    st.suffix = OPSUFFIX_FLOAT32;
                else if (!strcmp(s,"f64"))
                    st.suffix = OPSUFFIX_FLOAT64;
                else if (!strcmp(s,"f80"))
                    st.suffix = OPSUFFIX_FLOAT80;
                else if (!strcmp(s,"fbcd"))
                    st.suffix = OPSUFFIX_FLOATBCD;
                else if (*s == 0)
                    st.suffix = OPSUFFIX_NONE;
                else {
                    fprintf(stderr,"Name has unknown suffix %s\n",s);
                    return false;
                }
            }
        }
        else if (argc == 1) { /* b(r/m),b(reg) */
            st.format_str = s;

            char *fn;

            while (*s != 0) {
                if (isblank(*s)) {
                    s++;
                    continue;
                }

                fn = strchr(s,',');
                if (fn != NULL) *fn++ = 0;

                OpCodeDisplayArg arg;

                if (!strcmp(s,"i") || !strcmp(s,"w(i)")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_imm;
                }
                else if (!strcmp(s,"i2") || !strcmp(s,"w(i2)")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_imm;
                    arg.index = 1;
                }
                else if (!strncmp(s,"i[",2)) { // i[msb:lsb] bits msb to lsb inclusive
                    int upper=0,lower=0;

                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_imm;

                    s += 2;
                    upper = (int)strtol(s,&s,0);
                    if (*s == ':') {
                        lower = (int)strtol(s+1,&s,0);
                    }

                    if (lower > upper) {
                        fprintf(stderr,"Invalid immediate bit range\n");
                        return false;
                    }

                    arg.imm_bitwidth = (upper+1-lower);
                    arg.imm_bitoff = lower;
                }
                else if (!strcmp(s,"b(i)")) {
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.arg = OPDARG_imm;
                }
                else if (!strcmp(s,"b(i2)")) {
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.arg = OPDARG_imm;
                    arg.index = 1;
                }
                else if (!strcmp(s,"w(i+ip)")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_imm;
                    arg.ip_relative = true;
                }
                else if (!strcmp(s,"b(reg)")) {
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"b(r/m)")) {
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"w(reg)")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(r/m)")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"w16(reg)")) {
                    arg.argtype = OPDARGTYPE_WORD16;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w16(r/m)")) {
                    arg.argtype = OPDARGTYPE_WORD16;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"w32(reg)")) {
                    arg.argtype = OPDARGTYPE_WORD32;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w32(r/m)")) {
                    arg.argtype = OPDARGTYPE_WORD32;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"w64(reg)")) {
                    arg.argtype = OPDARGTYPE_WORD64;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w64(r/m)")) {
                    arg.argtype = OPDARGTYPE_WORD64;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"cr(reg)")) {
                    arg.argtype = OPDARGTYPE_CREG;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"cr(r/m)")) {
                    arg.argtype = OPDARGTYPE_CREG;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"dr(reg)")) {
                    arg.argtype = OPDARGTYPE_DREG;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"dr(r/m)")) {
                    arg.argtype = OPDARGTYPE_DREG;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"tr(reg)")) {
                    arg.argtype = OPDARGTYPE_TREG;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"tr(r/m)")) {
                    arg.argtype = OPDARGTYPE_TREG;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"b(a)")) { // AL/AX/EAX
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.argreg = OPREG_AL;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"b(b)")) { // BL/BX/EBX
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.argreg = OPREG_BL;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"b(c)")) { // CL/CX/ECX
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.argreg = OPREG_CL;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"b(d)")) { // DL/DX/EDX
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.argreg = OPREG_DL;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(a)")) { // AL/AX/EAX
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.argreg = OPREG_AX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(b)")) { // BL/BX/EBX
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.argreg = OPREG_BX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(c)")) { // CL/CX/ECX
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.argreg = OPREG_CX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(d)")) { // DL/DX/EDX
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.argreg = OPREG_DX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w16(d)")) { // DL/DX/EDX
                    arg.argtype = OPDARGTYPE_WORD16;
                    arg.argreg = OPREG_DX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"b(mem[i])")) {
                    arg.argtype = OPDARGTYPE_BYTE;
                    arg.arg = OPDARG_mem_imm;
                }
                else if (!strcmp(s,"w(mem[i])")) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_mem_imm;
                }
                else if (!strcmp(s,"seg(reg)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"seg(r/m)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"w(es)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_ES;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(cs)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_CS;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(ss)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_SS;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(ds)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_DS;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(fs)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_FS;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"w(gs)")) {
                    arg.argtype = OPDARGTYPE_SREG;
                    arg.argreg = OPREG_GS;
                    arg.arg = OPDARG_reg;
                }
                else if (!strncmp(s,"cr(",3) && isdigit(s[3])) {
                    arg.argtype = OPDARGTYPE_CREG;
                    arg.argreg = (opccReg)atoi(s+3);
                    arg.arg = OPDARG_reg;
                }
                else if (!strncmp(s,"dr(",3) && isdigit(s[3])) {
                    arg.argtype = OPDARGTYPE_DREG;
                    arg.argreg = (opccReg)atoi(s+3);
                    arg.arg = OPDARG_reg;
                }
                else if (!strncmp(s,"tr(",3) && isdigit(s[3])) {
                    arg.argtype = OPDARGTYPE_TREG;
                    arg.argreg = (opccReg)atoi(s+3);
                    arg.arg = OPDARG_reg;
                }
                else if (isdigit(*s)) {
                    arg.argtype = OPDARGTYPE_WORD;
                    arg.arg = OPDARG_immval;
                    arg.immval = strtoul(s,&s,0);
                }
                else if (!strcmp(s,"f32(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPU32;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"f64(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPU64;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"f80(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPU80;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"fbcd(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPUBCD;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"fenv(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPUENV;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"fstate(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPUSTATE;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"st(0)")) {
                    arg.argtype = OPDARGTYPE_FPUREG;
                    arg.arg = OPDARG_reg;
                    arg.argreg = OPDARG_ST0; // ST(0)
                }
                else if (!strcmp(s,"st(r/m)")) {
                    arg.argtype = OPDARGTYPE_FPUREG;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"mmx(reg)")) {
                    arg.argtype = OPDARGTYPE_MMX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"mmx(r/m)")) {
                    arg.argtype = OPDARGTYPE_MMX;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"sse(reg)")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"sse(r/m)")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"sse(vsidx)")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_vexsidx;
                }
                else if (!strcmp(s,"sse(xopsidx)")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_xopsidx;
                }
                else if (!strcmp(s,"sse(i[7:4])")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_i74;
                }
                else if (!strcmp(s,"avx(reg)")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_reg;
                }
                else if (!strcmp(s,"avx(r/m)")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_rm;
                }
                else if (!strcmp(s,"avx(vsidx)")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_vexsidx;
                }
                else if (!strcmp(s,"avx(xopsidx)")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_xopsidx;
                }
                else if (!strcmp(s,"avx(i[7:4])")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_i74;
                }
                else if (!strcmp(s,"vm32x(r/m)")) {
                    arg.argtype = OPDARGTYPE_SSE;
                    arg.arg = OPDARG_vm32x;
                }
                else if (!strcmp(s,"vm32y(r/m)")) {
                    arg.argtype = OPDARGTYPE_AVX;
                    arg.arg = OPDARG_vm32y;
                }
                else {
                    fprintf(stderr,"Unknown format spec %s\n",s);
                    return false;
                }

                st.disparg.push_back(arg);

                if (fn != NULL) s = fn;
                else break;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg\n");
            return false;
        }

        if (ns != NULL) s = ns;
        else break;

        argc++;
    }

    if (st.name.empty()) {
        if (!st.opsz32 && !st.addrsz32 && !st.illegal) {
            fprintf(stderr,"Instruction needs a name\n");
            return false;
        }
    }

    /* collect names */
    if (!st.name.empty() && !st.is_prefix)
        opcode_symbol_list[st.name] = true;

    last_opcodes.clear();
    if (st.xop) {
        unsigned char origop[20];
        unsigned int origopsz;

        memcpy(origop,st.ops,st.op);
        origopsz = st.op;

        if (st.oprange_min >= 0 || st.oprange_max >= 0) {
            fprintf(stderr,"Opcode ranges and XOP encoding not supported at this time\n");
            return false;
        }

        /* 3-byte XOP */
        {
            st.op = origopsz + 3;
            st.ops[0] = 0x8F;
            st.ops[1] = 0x00; // placeholder
            st.ops[2] = 0x00; // placeholder
            memcpy(st.ops+3,origop,origopsz);

            for (unsigned int v=0;v < 8;v++) {
                if (st.xopsidx >= 0 && st.xopsidx != v)
                    continue;

                // bit    7: R=0 (inverted)
                // bit    6: X=0 (inverted)
                // bit    5: B=0 (inverted)
                // bits 4-0: map_select (xopmap)
                st.ops[1] = 0xE0 + st.xopmap;
                // bit    7: W
                // bits 6-3: v3-v0 bits (inverted)
                // bit    2: L bit (256-wide SIMD)
                // bits 1-0: prefix
                st.ops[2] = 0x00 + (st.xopw ? 0x80 : 0x00) + ((v^15) << 3) + (st.xopsize == 256 ? 4 : 0) + st.xoppp;

                // do it
                if (true/*16-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap16))
                    return false;
                if (true/*32-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap32))
                    return false;
            }
        }
    }
    else if (st.vex) {
        unsigned char origop[20];
        unsigned int origopsz;

        memcpy(origop,st.ops,st.op);
        origopsz = st.op;

        if (st.oprange_min >= 0 || st.oprange_max >= 0) {
            fprintf(stderr,"Opcode ranges and VEX encoding not supported at this time\n");
            return false;
        }

        // FIXME: Do Intel processors demand the shortest VEX encoding for an instruction?
        //        Or is it OK to use either 2-byte or 3-byte VEX encoding to represent the same instruction?

        /* if a 2-byte VEX form is possible, encode it */
        if (st.vexlead == OP_VEX_LEAD_0F && st.vw == 0) {
            st.op = origopsz + 2;
            st.ops[0] = 0xC5;
            st.ops[1] = 0x00; // placeholder
            memcpy(st.ops+2,origop,origopsz);

            for (unsigned int v=0;v < 8;v++) {
                if (st.vexsidx >= 0 && st.vexsidx != v)
                    continue;

                // bit    7: R=0 (inverted)
                // bits 6-3: v3-v0 bits (inverted)
                // bit    2: L bit (256-wide SIMD)
                // bits 1-0: implied prefix
                st.ops[1] = 0x80 + ((v^15) << 3) + (st.vexsize == 256 ? 4 : 0) + st.vexprefix;

                // do it
                if (true/*16-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap16))
                    return false;
                if (true/*32-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap32))
                    return false;
            }
        }

        /* 3-byte VEX */
        {
            st.op = origopsz + 3;
            st.ops[0] = 0xC4;
            st.ops[1] = 0x00; // placeholder
            st.ops[2] = 0x00; // placeholder
            memcpy(st.ops+3,origop,origopsz);

            for (unsigned int v=0;v < 8;v++) {
                if (st.vexsidx >= 0 && st.vexsidx != v)
                    continue;

                // bit    7: R=0 (inverted)
                // bit    6: X=0 (inverted)
                // bits   5: B=0 (inverted)
                // bits 4-0: leading opcode byte pattern
                st.ops[1] = 0xE0 + st.vexlead;
                // bit    7: W=0
                // bits 6-3: v3-v0 bits (inverted)
                // bit    2: L bit (256-wide SIMD)
                // bits 1-0: implied prefix
                st.ops[2] = 0x00 + (st.vw ? 0x80 : 0x00) + ((v^15) << 3) + (st.vexsize == 256 ? 4 : 0) + st.vexprefix;

                // do it
                if (true/*16-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap16))
                    return false;
                if (true/*32-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap32))
                    return false;
            }
        }
    }
    else if (st.oprange_min < 0) {
        if (true/*16-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap16))
            return false;
        if (true/*32-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap32))
            return false;
    }
    else {
        for (unsigned int i=st.oprange_min;i <= st.oprange_max;i++) {
            assert((st.op+1) < sizeof(st.ops));
            st.ops[st.op++] = i;

            if (true/*16-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap16))
                return false;
            if (true/*32-bit*/ && !parse_opcode_def_gen1(/*&*/st,/*&*/opmap32))
                return false;

            st.op--;
        }
    }

    return true;
}

bool parse_opcodelist(void) {
    char line[1024],*s;
    char parse_line[1024];
    unsigned long lineno = 0;

    memset(line,0,sizeof(line));
    while (!feof(in_fp)) {
        if (ferror(in_fp)) {
            fprintf(stderr,"File I/O error on stdin\n");
            return false;
        }

        lineno++;
        if (fgets(line,sizeof(line)-1,in_fp) == NULL)
            break;

        opcc_chomp(line);
        s = line;
        while (isblank(*s)) s++;

        /* eat comment field */
        {
            char *semi = strchr(s,';');
            if (semi != NULL) *semi = 0;
        }

        /* skip blank lines */
        if (*s == 0) continue;

        /* skip CPP preprocessor directives */
        if (*s == '#') continue;

        strcpy(parse_line,line);
        if (!strncmp(s,"opcode ",7)) {
            s += 7;
            while (isblank(*s)) s++;

            if (!parse_opcode_def(line,lineno,s)) {
                fprintf(stderr," (%lu): %s\n",lineno,parse_line);
                return false;
            }
        }
        else if (!strncmp(s,"emucode:",8)) {
            s += 8;
            while (*s == '\t' || *s == ' ') s++;
            if (last_opcodes.empty()) {
                fprintf(stderr,"Emulator code must follow opcode\n");
                return false;
            }

            for (size_t i=0;i < last_opcodes.size();i++) {
                OpByte *b = last_opcodes[i];
                b->exec_code.push_back(s);
            }
        }
        else {
            fprintf(stderr,"Unknown input:\n");
            fprintf(stderr," (%lu): %s\n",lineno,parse_line);
            return false;
        }
    }

    return true;
}

#define OUTCODE_GEN_ALREADY_IPFB                    (1U << 0)
#define OUTCODE_GEN_DEFAULT_IS_GOTO_FALLBACK        (1U << 1)
#define OUTCODE_GEN_SKIP_MRM                        (1U << 2)
#define OUTCODE_GEN_CODE32                          (1U << 3)
#define OUTCODE_GEN_ADDR32                          (1U << 4)
#define OUTCODE_GEN_SKIP_IPFB                       (1U << 5)

void outcode_gen(const unsigned int codewidth,const unsigned int addrwidth,const uint8_t *opbase,const size_t opbaselen,OpByte &map,const unsigned int indent,const unsigned int flags,OpByte& maproot);

static const char *immnames[4] = {"opstate.imm","opstate.imm2","opstate.imm3","opstate.imm4"};
static const char *immnames_rip[4] = {"(opstate.imm+IPval())","(opstate.imm2+IPval())","(opstate.imm3+IPval())","(opstate.imm4+IPval())"};

void opcode_gen_case_statement_immarg_fetch_nopc(const string &indent_str,OpByte *submap) {
    unsigned int immc = 0;

    for (size_t i=0;i < submap->immarg.size();i++) {
        enum opccArgs a = submap->immarg[i];

        switch (a) {
            case OPARG_IB:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFB();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFB();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IBS:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFBsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFBsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFcodeW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFcodeW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IWA:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFaddrW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFaddrW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IWS:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFcodeWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFcodeWsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW16:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW16S:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFWsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW32:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFDW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFDW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW32S:
                if (immc != 0)  fprintf(out_fp,"%sopstate.imm%u=IPFDWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%sopstate.imm=IPFDWsigned();\n",indent_str.c_str());
                immc++;
                break;
            default:
                break;
        };
    }
}

void opcode_gen_case_statement_immarg_fetch(const string &indent_str,OpByte *submap) {
    unsigned int immc = 0;

    for (size_t i=0;i < submap->immarg.size();i++) {
        enum opccArgs a = submap->immarg[i];

        switch (a) {
            case OPARG_IB:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFB();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFB();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IBS:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFBsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFBsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFcodeW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFcodeW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IWA:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFaddrW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFaddrW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IWS:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFcodeWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFcodeWsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW16:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW16S:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFWsigned();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW32:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFDW();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFDW();\n",indent_str.c_str());
                immc++;
                break;
            case OPARG_IW32S:
                if (immc != 0)  fprintf(out_fp,"%s        opstate.imm%u=IPFDWsigned();\n",indent_str.c_str(),immc+1);
                else            fprintf(out_fp,"%s        opstate.imm=IPFDWsigned();\n",indent_str.c_str());
                immc++;
                break;
            default:
                break;
        };
    }
}

void opcode_gen_case_statement(const unsigned int codewidth,const unsigned int addrwidth,const uint8_t *opbase,const size_t opbaselen,OpByte &map,const unsigned int indent,const string &indent_str,OpByte *submap,const uint8_t op,uint32_t flags,OpByte& maproot) {
    bool emit_prefix_goto = false;
    unsigned int immc = 0;

    if (submap->vex_decompose_here == 3)
        fprintf(out_fp,"%s        opstate.vex.V = ((~opstate.op) >> 3) & 7;\n",indent_str.c_str());
    else if (submap->vex_decompose_here == 2)
        fprintf(out_fp,"%s        opstate.vex.V = ((~opstate.mrm.byte) >> 3) & 7;\n",indent_str.c_str());
    else if (submap->xop_decompose_here == 3)
        fprintf(out_fp,"%s        opstate.vex.V = ((~opstate.op) >> 3) & 7;/*NTS: Just re-use the VEX.V field*/\n",indent_str.c_str());

    if (!(flags & OUTCODE_GEN_SKIP_MRM)) {
        if (submap->modregrm) {
            if (generic1632) {
                if (cc_mode == MOD_EXECUTE) {
                    fprintf(out_fp,"%s        if (opstate.addr32) {\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_mrm_sib_disp_a32_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_LEA32(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                    fprintf(out_fp,"%s        } else {\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_mrm_sib_disp_a16_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_LEA16(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                    fprintf(out_fp,"%s        }\n",indent_str.c_str());
                }
                else {
                    fprintf(out_fp,"%s        if (opstate.addr32)\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_mrm_sib_disp_a32_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                    fprintf(out_fp,"%s        else\n",indent_str.c_str());
                    fprintf(out_fp,"%s            IPFB_mrm_sib_disp_a16_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                }
            }
            else {
                fprintf(out_fp,"%s        IPFB_mrm_sib_disp_a%u_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str(),addrwidth);
                if (cc_mode == MOD_EXECUTE) fprintf(out_fp,"%s        IPFB_LEA%u(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str(),addrwidth);
            }
        }
    }

    if (submap->modregrm && mprefix_exists && submap->opmap_valid) {
        size_t i=0;

        fprintf(out_fp,"%s        _x86decode_begin_code%u_addr%u_opcode_parse_",indent_str.c_str(),codewidth,addrwidth);
        for (;i < opbaselen;i++) fprintf(out_fp,"%02X",opbase[i]);
        fprintf(out_fp,"%02X",op);
        if (generic1632) fprintf(out_fp,"_generic");
        fprintf(out_fp,":\n");
    }

    if (submap->opsz32)
        fprintf(out_fp,"%s        opstate.prefix66 ^= 1;\n",indent_str.c_str());
    if (submap->addrsz32)
        fprintf(out_fp,"%s        opstate.prefix67 ^= 1;\n",indent_str.c_str());

    if (submap->illegal) {
        fprintf(out_fp,"%s        goto _x86decode_illegal_opcode;\n",indent_str.c_str());
        return;
    }

    /* then fetch other args */
    if (!(flags&OUTCODE_GEN_SKIP_IPFB))
        opcode_gen_case_statement_immarg_fetch(indent_str,submap);

    if (submap->opmap_valid) {
        uint8_t tmp[16];

        if (submap->isprefix) {
            /* don't generate sub-switch for prefixes, UNLESS a mandatory prefix. */
            if (submap->mprefix_exists_here) {
                unsigned int jcw=codewidth,jaw=addrwidth;
                unsigned int flags_l = 0;
                bool emit=false;

                if (submap->modregrm) {
                    fprintf(stderr,"mprefix+mod/reg/rm unsupported case\n");
                    abort();
                }

                if (!generic1632) {
                    if (submap->opsz32) {
                        flags_l ^= OUTCODE_GEN_CODE32;
                        if (jcw == 32) jcw = 16;
                        else if (jcw == 16) jcw = 32;
                    }
                    if (submap->addrsz32) {
                        flags_l ^= OUTCODE_GEN_ADDR32;
                        if (jaw == 32) jaw = 16;
                        else if (jaw == 16) jaw = 32;
                    }
                }

                if (submap->opsz32 || submap->addrsz32) {
                    if (generic1632) {
                        if (submap->opsz32)
                            fprintf(out_fp,"%s        opstate.code32 ^= 1;\n",indent_str.c_str());
                        else if (submap->addrsz32)
                            fprintf(out_fp,"%s        opstate.addr32 ^= 1;\n",indent_str.c_str());
                    }
                    else {
                        if (c32vars)
                            fprintf(out_fp,"%s        opstate.code32=%u; opstate.addr32=%u;\n",
                                    indent_str.c_str(),jcw==32?1:0,jaw==32?1:0);
                    }
                }

                /* oh joy. well, to make this work, we have to fetch the next byte into var "op".
                 * if the next byte is not 0x0F, then we have to goto to a label at the start of
                 * opcode parsing just after a fetch so the prefix decoding can work as normal.
                 * else, we fetch the next byte and switch case through it with this node to handle
                 * the instructions. If the case statement falls through then we goto a label to
                 * handle 0x0F opcode parsing as normal. */
                fprintf(out_fp,"%s        /* Mandatory prefix detection */\n",indent_str.c_str());
                fprintf(out_fp,"%s        opstate.op=IPFB();\n",indent_str.c_str());

                {
                    OpByte *submap2 = submap->opmap[0x0F];
                    if (submap2 != NULL && submap2->opmap_valid) {
                        assert(opbaselen < 14);
                        if (opbaselen != 0) memcpy(tmp,opbase,opbaselen);
                        tmp[opbaselen] = op;
                        tmp[opbaselen+1] = 0x0F;

                        fprintf(out_fp,"%s        %sif (opstate.op == 0x0F) {\n",indent_str.c_str(),emit?"else ":"");

                        outcode_gen(codewidth,addrwidth,tmp,opbaselen+2,*submap2,indent+3U,flags_l/*|OUTCODE_GEN_ALREADY_IPFB*/|OUTCODE_GEN_DEFAULT_IS_GOTO_FALLBACK,maproot);
                        emit_prefix_goto = true;
                        emit = true;

                        fprintf(out_fp,"%s        }\n",indent_str.c_str());
                    }
                }

                {
                    OpByte *submap2 = submap->opmap[0x90];
                    if (submap2 != NULL && !submap2->opmap_valid) {
                        string indent2 = indent_str + "    ";
                        assert(opbaselen < 14);
                        if (opbaselen != 0) memcpy(tmp,opbase,opbaselen);
                        tmp[opbaselen] = op;
                        tmp[opbaselen+1] = 0x90;

                        fprintf(out_fp,"%s        %sif (opstate.op == 0x90) {\n",indent_str.c_str(),emit?"else ":"");

                        opcode_gen_case_statement(codewidth,addrwidth,tmp,opbaselen+2,map,indent+1U,indent2,submap2,0x90,0,maproot);
                        emit_prefix_goto = true;
                        emit = true;

                        fprintf(out_fp,"%s        }\n",indent_str.c_str());
                    }
                }

                if (emit) {
                    fprintf(out_fp,"%s        else {\n",
                        indent_str.c_str());

                    if (generic1632)
                        fprintf(out_fp,"%s            goto _x86decode_begin_code%u_addr%u_opcode_parse__generic;\n",
                            indent_str.c_str(),codewidth,addrwidth);
                    else if (generic_case_1632 && (jcw != jaw) && (jcw != codewidth || jaw != addrwidth))
                        fprintf(out_fp,"%s            goto _x86decode_begin_code%u_addr%u_opcode_parse__generic;\n",
                            indent_str.c_str(),32/*FIXME*/,32/*FIXME*/);
                    else
                        fprintf(out_fp,"%s            goto _x86decode_begin_code%u_addr%u_opcode_parse_;\n",
                            indent_str.c_str(),jcw,jaw);

                    fprintf(out_fp,"%s        }\n",
                        indent_str.c_str());
                }
            }
        }
        else {
            assert(opbaselen < 15);
            if (opbaselen != 0) memcpy(tmp,opbase,opbaselen);
            tmp[opbaselen] = op;

            outcode_gen(codewidth,addrwidth,tmp,opbaselen+1,*submap,indent+2U,flags & (~(OUTCODE_GEN_ALREADY_IPFB)),maproot);
        }
    }
    else {
        if (cc_mode == MOD_EXECUTE && !submap->name.empty()) {
            for (size_t i=0;i < submap->exec_code.size();i++) {
                string &line = submap->exec_code[i];
                const char *line_s = line.c_str();
                const char *e;
                string ph;

                fprintf(out_fp,"%s        ",indent_str.c_str());
                while (*line_s != 0) {
                    if (*line_s == '@') {
                        line_s++;
                        e = strchr(line_s,'@');
                        if (e == NULL) e = line_s + strlen(line_s);
                        ph = string(line_s,(size_t)(e-line_s));
                        line_s = e;
                        if (*line_s == '@') line_s++;

                        if (ph == "codewidth") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.code32?32:16)");
                            else
                                fprintf(out_fp,"%u",codewidth);
                        }
                        else if (ph == "codewidthbytes") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.code32?4:2)");
                            else
                                fprintf(out_fp,"%u",codewidth>>3);
                        }
                        else if (ph == "codewidthmask") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.code32?0xFFFFFFFFUL:0xFFFFUL)");
                            else
                                fprintf(out_fp,"0x%lXUL",codewidth == 32 ? 0xFFFFFFFFUL : 0xFFFFUL);
                        }
                        else if (ph == "code32") {
                            if (generic1632)
                                fprintf(out_fp,"opstate.code32");
                            else
                                fprintf(out_fp,"%u",codewidth == 32 ? 1 : 0);
                        }
                        else if (ph == "code16") {
                            if (generic1632)
                                fprintf(out_fp,"!opstate.code32");
                            else
                                fprintf(out_fp,"%u",codewidth == 16 ? 1 : 0);
                        }
                        else if (ph == "addrwidth") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.addr32?32:16)");
                            else
                                fprintf(out_fp,"%u",addrwidth);
                        }
                        else if (ph == "addrwidthbytes") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.addr32?4:2)");
                            else
                                fprintf(out_fp,"%u",addrwidth>>3);
                        }
                        else if (ph == "addrwidthmask") { // becomes an integer value
                            if (generic1632)
                                fprintf(out_fp,"(opstate.addr32?0xFFFFFFFFUL:0xFFFFUL)");
                            else
                                fprintf(out_fp,"0x%lXUL",addrwidth == 32 ? 0xFFFFFFFFUL : 0xFFFFUL);
                        }
                        else if (ph == "addr32") {
                            if (generic1632)
                                fprintf(out_fp,"opstate.addr32");
                            else
                                fprintf(out_fp,"%u",addrwidth == 32 ? 1 : 0);
                        }
                        else if (ph == "addr16") {
                            if (generic1632)
                                fprintf(out_fp,"!opstate.addr32");
                            else
                                fprintf(out_fp,"%u",addrwidth == 16 ? 1 : 0);
                        }
                        else if (ph == "i") {
                            fprintf(out_fp,"opstate.imm");
                        }
                        else if (ph == "reg") {
                            if (submap->reg_from_opcode)
                                fprintf(out_fp,"(opstate.op&7)");
                            else
                                fprintf(out_fp,"opstate.mrm.reg()");
                        }
                        else if (ph == "disp") {
                            fprintf(out_fp,"opstate.disp");
                        }
                        else {
                            fprintf(stderr,"WARNING: unknown placeholder '%s'\n",ph.c_str());
                        }
                    }
                    else {
                        fprintf(out_fp,"%c",*line_s);
                        line_s++;
                    }
                }
                fprintf(out_fp,";\n");
            }
        }
        else if (cc_mode == MOD_DECOMPILE && !submap->name.empty()) {
            string fmtargs;
            char tmp[128];

            if (!submap->isprefix)
                fprintf(out_fp,"%s        opstate.opcode = OPCODE_%s;\n",indent_str.c_str(),submap->name.c_str());
            else if (submap->segoverride != OPSEG_NONE)
                fprintf(out_fp,"%s        opstate.segoverride = OPSEG_%s;\n",indent_str.c_str(),opseg2string(submap->segoverride));
            else if (submap->name == "REPZ" || submap->name == "REPNZ" || submap->name == "REPC" || submap->name == "REPNC")
                fprintf(out_fp,"%s        opstate.repmode = OPREP_%s;\n",indent_str.c_str(),submap->name.c_str());
            else
                fprintf(out_fp,"%s        opstate.prefix_%s = 1;\n",indent_str.c_str(),submap->name.c_str());

            if (!submap->isprefix) {
            fprintf(out_fp,"%s        ipw += snprintf(ipw,(size_t)(ipwf-ipw),\"",indent_str.c_str());
            if (generic1632 && submap->suffix == OPSUFFIX_WORD) {
                fprintf(out_fp,"%s%%s",submap->name.c_str());
                fmtargs += ",opstate.code32?\"d\":\"w\"";
            }
            else {
                fprintf(out_fp,"%s%s",submap->name.c_str(),suffix_str(submap->suffix,codewidth));
            }

            if (submap->isprefix) {
                if (!submap->name.empty()) fprintf(out_fp," ");
            }
            else if (submap->disparg.size() != 0)
                fprintf(out_fp," ");

            /* then display arguments as directed by format string */
            for (size_t i=0;i < submap->disparg.size();i++) {
                OpCodeDisplayArg &arg = submap->disparg[i];

                if (arg.arg == OPDARG_immval) {
                    if (arg.immval > 9)
                        fprintf(out_fp,"0x%lX",(unsigned long)arg.immval);
                    else
                        fprintf(out_fp,"%lu",(unsigned long)arg.immval);
                }
                else if (arg.arg == OPDARG_mem_imm) {
                    enum opccArgs argt = OPARG_NONE;
                    const char **imn = immnames;

                    if (arg.ip_relative) imn = immnames_rip;

                    if (arg.index < submap->immarg.size())
                        argt = submap->immarg[arg.index];

                    switch (argt) {
                        case OPARG_IB:
                        case OPARG_IBS:
                            fprintf(out_fp,"[0x%%02lX]");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",addrwidth,imn[arg.index]);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&(opstate.addr32?0xFFFFFFFFUL:0xFFFFUL)";
                            break;
                        case OPARG_IW:
                        case OPARG_IWA:
                        case OPARG_IWS:
                            if (generic1632) {
                                fprintf(out_fp,"[0x%%0*lX]");
                                fmtargs += ",opstate.addr32?8:4";
                            }
                            else {
                                if (addrwidth == 32)
                                    fprintf(out_fp,"[0x%%08lX]");
                                else
                                    fprintf(out_fp,"[0x%%04lX]");
                            }

                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",addrwidth,imn[arg.index]);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&(opstate.addr32?0xFFFFFFFFUL:0xFFFFUL)";
                            break;
                        case OPARG_IW16:
                        case OPARG_IW16S:
                            fprintf(out_fp,"[0x%%04lX]");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",addrwidth,imn[arg.index]);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&0xFFFFUL";
                            break;
                        case OPARG_IW32:
                        case OPARG_IW32S:
                            fprintf(out_fp,"[0x%%08lX]");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",addrwidth,imn[arg.index]);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&0xFFFFFFFFUL";
                            break;
                        default:
                            break;
                    };

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            fprintf(out_fp,"b");
                            break;
                        case OPDARGTYPE_WORD:
                            fprintf(out_fp,"w");
                            break;
                        case OPDARGTYPE_WORD16:
                            fprintf(out_fp,"w16");
                            break;
                        case OPDARGTYPE_WORD32:
                            fprintf(out_fp,"w32");
                            break;
                        case OPDARGTYPE_WORD64:
                            fprintf(out_fp,"w64");
                            break;
                        case OPDARGTYPE_FPU32:
                            fprintf(out_fp,"f32");
                            break;
                        case OPDARGTYPE_FPU64:
                            fprintf(out_fp,"f64");
                            break;
                        case OPDARGTYPE_FPU80:
                            fprintf(out_fp,"f80");
                            break;
                        case OPDARGTYPE_FPUBCD:
                            fprintf(out_fp,"fbcd");
                            break;
                        case OPDARGTYPE_FPUENV:
                            fprintf(out_fp,"fenv");
                            break;
                        case OPDARGTYPE_FPUSTATE:
                            fprintf(out_fp,"fstate");
                            break;
                        default:
                            break;
                    }
                }
                else if (arg.arg == OPDARG_rm || arg.arg == OPDARG_vm32x || arg.arg == OPDARG_vm32y) {
                    const char *szp;
                    const char *rc = "RC_REG";
                    const char *suffix = codewidth == 32 ? "d" : "w";

                    if (generic1632)
                        szp = "opstate.code32?4:2";
                    else
                        szp = codewidth == 32 ? "4" : "2";

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            szp = "1";
                            suffix = "b";
                            break;
                        case OPDARGTYPE_WORD:
                            // already default
                            break;
                        case OPDARGTYPE_WORD16:
                            szp = "2";
                            suffix = "w16";
                            break;
                        case OPDARGTYPE_WORD32:
                            szp = "4";
                            suffix = "w32";
                            break;
                        case OPDARGTYPE_WORD64:
                            szp = "8";
                            suffix = "w64";
                            break;
                        case OPDARGTYPE_FPUREG:
                            szp = "8";
                            rc = "RC_FPUREG";
                            suffix = "";
                            break;
                        case OPDARGTYPE_FPU32:
                            szp = "4";
                            rc = "RC_FPUREG";
                            suffix = "f32";
                            break;
                        case OPDARGTYPE_FPU64:
                            szp = "8";
                            rc = "RC_FPUREG";
                            suffix = "f64";
                            break;
                        case OPDARGTYPE_FPU80:
                            szp = "10";
                            rc = "RC_FPUREG";
                            suffix = "f80";
                            break;
                        case OPDARGTYPE_FPUBCD:
                            szp = "10";
                            rc = "RC_FPUREG";
                            suffix = "fbcd";
                            break;
                        case OPDARGTYPE_FPUENV:
                            szp = "0";
                            suffix = "fenv";
                            break;
                        case OPDARGTYPE_FPUSTATE:
                            szp = "0";
                            suffix = "fstate";
                            break;
                        case OPDARGTYPE_MMX:
                            szp = "8";
                            rc = "RC_MMXREG";
                            suffix = "";
                            break;
                        case OPDARGTYPE_SSE:
                            szp = "16";
                            rc = "RC_SSEREG";
                            suffix = "";
                            break;
                        case OPDARGTYPE_AVX:
                            szp = "32";
                            rc = "RC_AVXREG";
                            suffix = "";
                            break;
                        default:
                            break;
                    }

                    fprintf(out_fp,"%%s");
                    if (generic1632) {
                        fmtargs += ",IPDecPrint1632(opstate.addr32,opstate.mrm,opstate.sib,opstate.disp,";
                    }
                    else {
                        if (addrwidth == 32)
                            fmtargs += ",IPDecPrint32(opstate.mrm,opstate.sib,opstate.disp,";
                        else
                            fmtargs += ",IPDecPrint16(opstate.mrm,opstate.disp,";
                    }

                    fmtargs += szp;
                    fmtargs += ",";
                    fmtargs += rc;
                    if (generic1632 && arg.argtype == OPDARGTYPE_WORD) {
                        fmtargs += ",opstate.code32?\"d\":\"w\"";
                    }
                    else {
                        fmtargs += ",\"";
                        fmtargs += suffix;
                        fmtargs += "\"";
                    }

                    if (arg.arg == OPDARG_vm32x)
                        fmtargs += ",MRM_VSIB_X";
                    else if (arg.arg == OPDARG_vm32y)
                        fmtargs += ",MRM_VSIB_Y";

                    fmtargs += ")";
                }
                else if (arg.arg == OPDARG_reg) {
                    const char *regname = "opstate.mrm.reg()";
                    char regn[32];

                    if (!submap->modregrm) {
                        if (submap->reg_from_opcode)
                            regname = "opstate.op&7";
                    }

                    if (arg.argreg != OPREG_NONE) {
                        sprintf(regn,"%u",arg.argreg);
                        regname = regn;
                    }

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[1][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD:
                            fprintf(out_fp,"%%s");
                            if (generic1632) {
                                fmtargs += ",CPUregsN[opstate.code32?4:2][";
                            }
                            else {
                                if (codewidth == 32)
                                    fmtargs += ",CPUregsN[4][";
                                else
                                    fmtargs += ",CPUregsN[2][";
                            }
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD16:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[2][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD32:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[4][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_FPUREG:
                            fprintf(out_fp,"ST(%%u)");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SREG:
                            // todo: use CPUsregs_80386[] if 386 or higher
                            //       else use CPUsregs_8086[]
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUsregs_80386[";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_CREG:
                            fprintf(out_fp,"CR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_DREG:
                            fprintf(out_fp,"DR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                         case OPDARGTYPE_TREG:
                            fprintf(out_fp,"TR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_MMX:
                            fprintf(out_fp,"MM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SSE:
                            fprintf(out_fp,"XMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_AVX:
                            fprintf(out_fp,"YMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        default:
                            break;
                    }
                }
                else if (arg.arg == OPDARG_imm) {
                    enum opccArgs argt = OPARG_NONE;
                    const char **imn = immnames;

                    if (arg.ip_relative) imn = immnames_rip;

                    if (arg.index < submap->immarg.size())
                        argt = submap->immarg[arg.index];

                    const char *imn_str = imn[arg.index];

                    char imn_str_printf[64];

                    if (arg.imm_bitwidth != 0) {
                        /* we need to take the variable and bit shift & mask it */
                        sprintf(imn_str_printf,"((%s >> %u) & ((1 << %u) - 1))",
                            imn_str,arg.imm_bitoff,arg.imm_bitwidth);
                        imn_str = imn_str_printf;
                    }

                    switch (argt) {
                        case OPARG_IB:
                        case OPARG_IBS:
                            fprintf(out_fp,"0x%%02lX");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",codewidth,imn_str);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&(opstate.code32?0xFFFFFFFFUL:0xFFFFUL)";
                            break;
                        case OPARG_IWA:
                            if (generic1632) {
                                fprintf(out_fp,"0x%%0*lX");
                                fmtargs += ",opstate.addr32?8:4";
                            }
                            else {
                                if (addrwidth == 32)
                                    fprintf(out_fp,"0x%%08lX");
                                else
                                    fprintf(out_fp,"0x%%04lX");
                            }

                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",addrwidth,imn_str);
                            if (generic1632) fmtargs += "&(opstate.addr32?0xFFFFFFFFUL:0xFFFFUL)";
                            fmtargs += tmp;
                            break;
                        case OPARG_IW:
                        case OPARG_IWS:
                            if (generic1632) {
                                fprintf(out_fp,"0x%%0*lX");
                                fmtargs += ",opstate.code32?8:4";
                            }
                            else {
                                if (codewidth == 32)
                                    fprintf(out_fp,"0x%%08lX");
                                else
                                    fprintf(out_fp,"0x%%04lX");
                            }

                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",codewidth,imn_str);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&(opstate.code32?0xFFFFFFFFUL:0xFFFFUL)";
                            break;
                        case OPARG_IW16:
                        case OPARG_IW16S:
                            fprintf(out_fp,"0x%%04lX");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",codewidth,imn_str);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&0xFFFFUL";
                            break;
                        case OPARG_IW32:
                        case OPARG_IW32S:
                            fprintf(out_fp,"0x%%08lX");
                            sprintf(tmp,",(unsigned long)((uint%u_t)%s)",codewidth,imn_str);
                            fmtargs += tmp;
                            if (generic1632) fmtargs += "&0xFFFFFFFFUL";
                            break;
                        default:
                            break;
                    };
                }
                else if (arg.arg == OPDARG_vexsidx) {
                    const char *regname = "opstate.vex.V";
                    char regn[32];

                    if (arg.argreg != OPREG_NONE) {
                        sprintf(regn,"%u",arg.argreg);
                        regname = regn;
                    }

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[1][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD:
                            fprintf(out_fp,"%%s");
                            if (generic1632) {
                                fmtargs += ",CPUregsN[opstate.code32?4:2][";
                            }
                            else {
                                if (codewidth == 32)
                                    fmtargs += ",CPUregsN[4][";
                                else
                                    fmtargs += ",CPUregsN[2][";
                            }
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD16:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[2][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD32:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[4][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_FPUREG:
                            fprintf(out_fp,"ST(%%u)");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SREG:
                            // todo: use CPUsregs_80386[] if 386 or higher
                            //       else use CPUsregs_8086[]
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUsregs_80386[";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_CREG:
                            fprintf(out_fp,"CR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_DREG:
                            fprintf(out_fp,"DR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                         case OPDARGTYPE_TREG:
                            fprintf(out_fp,"TR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_MMX:
                            fprintf(out_fp,"MM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SSE:
                            fprintf(out_fp,"XMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_AVX:
                            fprintf(out_fp,"YMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        default:
                            break;
                    }
                }
                else if (arg.arg == OPDARG_i74) {
                    const char *regname = "(opstate.imm>>4)&15";
                    char regn[32];

                    if (arg.argreg != OPREG_NONE) {
                        sprintf(regn,"%u",arg.argreg);
                        regname = regn;
                    }

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[1][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD:
                            fprintf(out_fp,"%%s");
                            if (generic1632) {
                                fmtargs += ",CPUregsN[opstate.code32?4:2][";
                            }
                            else {
                                if (codewidth == 32)
                                    fmtargs += ",CPUregsN[4][";
                                else
                                    fmtargs += ",CPUregsN[2][";
                            }
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD16:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[2][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD32:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[4][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_FPUREG:
                            fprintf(out_fp,"ST(%%u)");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SREG:
                            // todo: use CPUsregs_80386[] if 386 or higher
                            //       else use CPUsregs_8086[]
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUsregs_80386[";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_CREG:
                            fprintf(out_fp,"CR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_DREG:
                            fprintf(out_fp,"DR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                         case OPDARGTYPE_TREG:
                            fprintf(out_fp,"TR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_MMX:
                            fprintf(out_fp,"MM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SSE:
                            fprintf(out_fp,"XMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_AVX:
                            fprintf(out_fp,"YMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        default:
                            break;
                    }
                }
                else if (arg.arg == OPDARG_xopsidx) {
                    const char *regname = "opstate.vex.V"; // NTS: we re-use the VEX struct
                    char regn[32];

                    if (arg.argreg != OPREG_NONE) {
                        sprintf(regn,"%u",arg.argreg);
                        regname = regn;
                    }

                    switch (arg.argtype) {
                        case OPDARGTYPE_BYTE:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[1][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD:
                            fprintf(out_fp,"%%s");
                            if (generic1632) {
                                fmtargs += ",CPUregsN[opstate.code32?4:2][";
                            }
                            else {
                                if (codewidth == 32)
                                    fmtargs += ",CPUregsN[4][";
                                else
                                    fmtargs += ",CPUregsN[2][";
                            }
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD16:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[2][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_WORD32:
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUregsN[4][";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_FPUREG:
                            fprintf(out_fp,"ST(%%u)");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SREG:
                            // todo: use CPUsregs_80386[] if 386 or higher
                            //       else use CPUsregs_8086[]
                            fprintf(out_fp,"%%s");
                            fmtargs += ",CPUsregs_80386[";
                            fmtargs += regname;
                            fmtargs += "]";
                            break;
                        case OPDARGTYPE_CREG:
                            fprintf(out_fp,"CR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_DREG:
                            fprintf(out_fp,"DR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                         case OPDARGTYPE_TREG:
                            fprintf(out_fp,"TR%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_MMX:
                            fprintf(out_fp,"MM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_SSE:
                            fprintf(out_fp,"XMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        case OPDARGTYPE_AVX:
                            fprintf(out_fp,"YMM%%u");
                            fmtargs += ",";
                            fmtargs += regname;
                            break;
                        default:
                            break;
                    }
                }

                if ((i+1) < submap->disparg.size()) fprintf(out_fp,",");
            }

            fprintf(out_fp,"\"");
            if (!fmtargs.empty()) fprintf(out_fp,"%s",fmtargs.c_str());
            fprintf(out_fp,");\n");
            }
        }
    }

    if (submap->isprefix && !emit_prefix_goto) {
        unsigned int jcw=codewidth,jaw=addrwidth;

        if (submap->opsz32) {
            if (jcw == 32) jcw = 16;
            else if (jcw == 16) jcw = 32;
        }
        if (submap->addrsz32) {
            if (jaw == 32) jaw = 16;
            else if (jaw == 16) jaw = 32;
        }

        if (submap->opsz32 || submap->addrsz32) {
            if (generic1632) {
                if (submap->opsz32)
                    fprintf(out_fp,"%s        opstate.code32 ^= 1;\n",indent_str.c_str());
                else if (submap->addrsz32)
                    fprintf(out_fp,"%s        opstate.addr32 ^= 1;\n",indent_str.c_str());

                fprintf(out_fp,"%s        if (opstate.code32 && opstate.addr32)\n",indent_str.c_str());
                fprintf(out_fp,"%s            goto _x86decode_after_prefix_386override_code32_addr32;\n",indent_str.c_str());
                fprintf(out_fp,"%s        else if (opstate.code32)\n",indent_str.c_str());
                fprintf(out_fp,"%s            goto _x86decode_after_prefix_386override_code32_addr16;\n",indent_str.c_str());
                fprintf(out_fp,"%s        else if (opstate.addr32)\n",indent_str.c_str());
                fprintf(out_fp,"%s            goto _x86decode_after_prefix_386override_code16_addr32;\n",indent_str.c_str());
                fprintf(out_fp,"%s        else\n",indent_str.c_str());
                fprintf(out_fp,"%s            goto _x86decode_after_prefix_386override_code16_addr16;\n",indent_str.c_str());
            }
            else {
                if (c32vars)
                    fprintf(out_fp,"%s        opstate.code32=%u; opstate.addr32=%u;\n",
                        indent_str.c_str(),jcw==32?1:0,jaw==32?1:0);

                fprintf(out_fp,"%s        goto _x86decode_after_prefix_386override_code%u_addr%u;\n",
                    indent_str.c_str(),jcw,jaw);
            }
        }
        else {
            fprintf(out_fp,"%s        goto _x86decode_after_prefix_code%u_addr%u;\n",
                indent_str.c_str(),jcw,jaw);
        }
    }
    else {
        fprintf(out_fp,"%s        break;\n",indent_str.c_str());
    }
}

void outcode_gen(const unsigned int codewidth,const unsigned int addrwidth,const uint8_t *opbase,const size_t opbaselen,OpByte &map,const unsigned int indent,const unsigned int flags,OpByte& maproot) {
    OpByte *submap,*submap2;
    string indent_str;
    bool reg_enum=false;
    bool ipfb_combined=false;

    for (unsigned int ind=0;ind < indent;ind++)
        indent_str += "    ";

    if (opbaselen == 0) {
        fprintf(out_fp,"/* BEGIN AUTOGENERATED CODE */\n");
        if (generic1632) {
            fprintf(out_fp,"/* generic 16/32-bit */\n");
        }
        else {
            fprintf(out_fp,"/* code width = %u     addr width = %u */\n",codewidth,addrwidth);
            fprintf(out_fp,"/* expect host code to prefix this with */\n");
            fprintf(out_fp,"/*   _x86decode_begin_code%u_addr%u: */\n",codewidth,addrwidth);
            fprintf(out_fp,"/*   _x86decode_after_prefix_code%u_addr%u: */\n",codewidth,addrwidth);
        }
        fprintf(out_fp,"/* expect host code to provide: */\n");
        fprintf(out_fp,"/*   uint8_t op;               opcode byte */\n");
        fprintf(out_fp,"/*   uint%u_t imm,imm2,imm3,imm4; immediate %u-bit */\n",codewidth,codewidth);
        fprintf(out_fp,"/*   struct x86ModRegRm mrm;   mod/reg/rm byte */\n");
        fprintf(out_fp,"/*   struct x86ScaleIndexBase sib; scalar/index/base byte */\n");
        fprintf(out_fp,"/*   uint%u_t disp;            displacement */\n",addrwidth);
        fprintf(out_fp,"/* expect host code to provide: */\n");
        fprintf(out_fp,"/*   uint8_t IPFB();           fetch byte at instruction pointer */\n");
        fprintf(out_fp,"/*   uint16_t IPFW();          fetch word at instruction pointer */\n");
        fprintf(out_fp,"/*   uint32_t IPFDW();         fetch dword at instruction pointer */\n");
        fprintf(out_fp,"/*   uint%u_t IPFcodeW();      fetch %u-bit word at instruction pointer */\n",codewidth,codewidth);
        fprintf(out_fp,"/*   uint%u_t IPFaddrW();      fetch %u-bit word at instruction pointer */\n",addrwidth,addrwidth);
        fprintf(out_fp,"/*   void IPFB_mrm_sib_disp_a%u_read(mrm,sib,disp);   read mod/reg/rm, sib, displacement */\n",codewidth);

        if (cc_mode == MOD_DECOMPILE) {
            fprintf(out_fp,"/* expect host to provide: */\n");
            fprintf(out_fp,"/*   x86_offset_t IPDecIP; */\n");
            fprintf(out_fp,"/*   char IPDecStr[256]; */\n");
            fprintf(out_fp,"/*   char *ipw,*ipwf; */\n");
            fprintf(out_fp,"/*   x86_offset_t IPval(void); */\n");
            fprintf(out_fp,"/*   signed char segoverride = -1; */\n");
            fprintf(out_fp,"/*   unsigned int opcode = 0; */\n");
        }
    }
    else {
        fprintf(out_fp,"/* Opcodes starting with ");
        for (size_t i=0;i < opbaselen;i++) fprintf(out_fp,"%02Xh ",opbase[i]);
        fprintf(out_fp,"*/\n");
    }

    if (map.amd3dnow_here) {
        /* AMD 3DNow! 
         *
         * 0x0F 0x0F mod/reg/rm <opcode> */
        fprintf(out_fp,"%s/* AMD 3DNow! encoding 0x0F 0x0F mod/reg/rm <opcode> */\n",indent_str.c_str());
        if (generic1632) {
            if (cc_mode == MOD_EXECUTE) {
                fprintf(out_fp,"%sif (opstate.addr32) {\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_mrm_sib_disp_a32_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_LEA32(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                fprintf(out_fp,"%s} else {\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_mrm_sib_disp_a16_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_LEA16(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                fprintf(out_fp,"%s}\n",indent_str.c_str());
            }
            else {
                fprintf(out_fp,"%sif (opstate.addr32)\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_mrm_sib_disp_a32_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
                fprintf(out_fp,"%selse\n",indent_str.c_str());
                fprintf(out_fp,"%s    IPFB_mrm_sib_disp_a16_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str());
            }
        }
        else {
            fprintf(out_fp,"%sIPFB_mrm_sib_disp_a%u_read(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str(),addrwidth);
            if (cc_mode == MOD_EXECUTE) fprintf(out_fp,"%sIPFB_LEA%u(opstate.mrm,opstate.sib,opstate.disp);\n",indent_str.c_str(),addrwidth);
        }

        fprintf(out_fp,"%sswitch (opstate.op=IPFB()) {\n",indent_str.c_str());
    }
    else if (map.modregrm) {
        bool is_modreg_pattern = true;
        bool is_reg_pattern = false;

        /* first scan: is the opcode pattern such we can encode it as a switch (mrm.reg()) for Intel's /X syntax? */
        for (unsigned int i=0;i < 0x100;i += 8) {
            bool is_reg_block = false;

            if (map.opmap[i+0] == NULL &&
                map.opmap[i+1] == NULL &&
                map.opmap[i+2] == NULL &&
                map.opmap[i+3] == NULL &&
                map.opmap[i+4] == NULL &&
                map.opmap[i+5] == NULL &&
                map.opmap[i+6] == NULL &&
                map.opmap[i+7] == NULL) {
                is_reg_block = true;
            }
            else if (
                map.opmap[i+0] != NULL &&
                map.opmap[i+1] != NULL &&
                map.opmap[i+2] != NULL &&
                map.opmap[i+3] != NULL &&
                map.opmap[i+4] != NULL &&
                map.opmap[i+5] != NULL &&
                map.opmap[i+6] != NULL &&
                map.opmap[i+7] != NULL) {
                if (*(map.opmap[i+0]) == *(map.opmap[i+1]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+2]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+3]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+4]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+5]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+6]) &&
                    *(map.opmap[i+0]) == *(map.opmap[i+7])) {
                    is_reg_block = true;
                }
            }

            if (!is_reg_block)
                is_modreg_pattern = false;
        }

        if (is_modreg_pattern) {
            is_reg_pattern = true;
            for (unsigned int reg=0;reg < 0x40;reg += 8) {
                for (unsigned int mod=0x40;mod < 0x100;mod += 0x40) {
                    bool match = false;

                    if (map.opmap[reg] == NULL && map.opmap[reg+mod] == NULL) {
                        match = true;
                    }
                    else if (map.opmap[reg] != NULL && map.opmap[reg+mod] != NULL) {
                        if (*(map.opmap[reg]) == *(map.opmap[reg+mod]))
                            match = true;
                    }

                    if (!match)
                        is_reg_pattern = false;
                }
            }
        }

        if (is_reg_pattern) {
            // if all have the same immediate args then we can combine the IPFB() calls at the top
            {
                OpByte *fmap,*smap;
                bool f=true;

                fmap = map.opmap[0x00];
                for (unsigned int op=0x08;op < 0x40;op += 8) { // reg 0...7
                    smap = map.opmap[op];

                    if (fmap == NULL || smap == NULL) {
                        f = false;
                        break;
                    }
                    else if (fmap->already != smap->already) {
                        f = false;
                        break;
                    }
                    else if (fmap->immarg.size() != smap->immarg.size()) {
                        f = false;
                        break;
                    }
                    else if (fmap->immarg != smap->immarg) {
                        f = false;
                        break;
                    }
                }

                ipfb_combined = f;
            }

            // emit
            if (ipfb_combined)
                opcode_gen_case_statement_immarg_fetch_nopc(indent_str,map.opmap[0x00]);

            fprintf(out_fp,"%sswitch (opstate.mrm.reg()) {\n",indent_str.c_str());
            reg_enum = true;
        }
        else {
            fprintf(out_fp,"%sswitch (opstate.mrm.byte) {\n",indent_str.c_str());
        }
    }
    else {
        if (mprefix_exists) {
            size_t i=0;

            if (!(flags & OUTCODE_GEN_ALREADY_IPFB))
                fprintf(out_fp,"%sopstate.op=IPFB();\n",indent_str.c_str());

            fprintf(out_fp,"%s_x86decode_begin_code%u_addr%u_opcode_parse_",indent_str.c_str(),codewidth,addrwidth);
            for (;i < opbaselen;i++) fprintf(out_fp,"%02X",opbase[i]);
            if (generic1632) fprintf(out_fp,"_generic");
            fprintf(out_fp,":\n");

            fprintf(out_fp,"%sswitch (opstate.op) {\n",indent_str.c_str());
        }
        else {
            fprintf(out_fp,"%sswitch (opstate.op=IPFB()) {\n",indent_str.c_str());
        }
    }

    if (reg_enum) {
        // we can enumerate using only the REG field of mod/reg/rm
        assert(map.modregrm == true);

        for (unsigned int op=0;op < 0x40;op += 8) { // reg 0...7
            submap = map.opmap[op];
            if (submap != NULL && !submap->already) {
                for (unsigned int op2=op;op2 < 0x40;op2 += 8) {
                    submap2 = map.opmap[op2];

                    if (submap2 != NULL && !submap2->already && (op == op2 || *submap == *submap2)) {
                        fprintf(out_fp,"%s    case %u: /* ",indent_str.c_str(),op2 >> 3);
                        for (size_t i=0;i < opbaselen;i++) fprintf(out_fp,"%02Xh ",opbase[i]);
                        fprintf(out_fp,"%02Xh ",op2);
                        fprintf(out_fp,"%s%s %s ",submap2->name.c_str(),suffix_str(submap2->suffix,codewidth),submap2->format_str.c_str());
                        if (map.modregrm) fprintf(out_fp,"reg=%u ",(op2>>3)&7);
                        else if (submap2->reg_from_opcode) fprintf(out_fp,"reg=%u ",op2&7);
                        fprintf(out_fp,"     spec: %s ",submap2->opspec.c_str());
                        fprintf(out_fp,"*/\n");
                        submap2->already = true;
                    }
                }

                opcode_gen_case_statement(codewidth,addrwidth,opbase,opbaselen,map,indent,indent_str,submap,op,
                    flags | (ipfb_combined ? OUTCODE_GEN_SKIP_IPFB : 0),maproot);
            }

            if (submap == NULL) {
                fprintf(out_fp,"%s    /* reg %u not defined */\n",indent_str.c_str(),op >> 3);
            }
        }
    }
    else {
        for (unsigned int op=0;op < 0x100;op++) {
            submap = map.opmap[op];
            if (submap != NULL && !submap->already) {
                for (unsigned int op2=op;op2 < 0x100;op2++) {
                    submap2 = map.opmap[op2];

                    if (submap2 != NULL && !submap2->already && (op == op2 || *submap == *submap2)) {
                        fprintf(out_fp,"%s    case 0x%02X: /* ",indent_str.c_str(),op2);
                        for (size_t i=0;i < opbaselen;i++) fprintf(out_fp,"%02Xh ",opbase[i]);
                        fprintf(out_fp,"%02Xh ",op2);
                        fprintf(out_fp,"%s%s %s ",submap2->name.c_str(),suffix_str(submap2->suffix,codewidth),submap2->format_str.c_str());
                        if (map.modregrm) fprintf(out_fp,"mod=%u reg=%u rm=%u ",op2>>6,(op2>>3)&7,op2&7);
                        else if (submap2->reg_from_opcode) fprintf(out_fp,"reg=%u ",op2&7);
                        fprintf(out_fp,"     spec: %s ",submap2->opspec.c_str());
                        fprintf(out_fp,"*/\n");
                        submap2->already = true;
                    }
                }

                opcode_gen_case_statement(codewidth,addrwidth,opbase,opbaselen,map,indent,indent_str,submap,op,
                    flags | (map.amd3dnow_here ? OUTCODE_GEN_SKIP_MRM : 0),maproot);
            }

            if (submap == NULL) {
                fprintf(out_fp,"%s    /* opcode ",indent_str.c_str());
                for (size_t i=0;i < opbaselen;i++) fprintf(out_fp,"%02Xh ",opbase[i]);
                fprintf(out_fp,"%02Xh ",op);
                fprintf(out_fp," not defined */\n");
            }
        }
    }
    fprintf(out_fp,"%s    default:\n",indent_str.c_str());

    if (flags & OUTCODE_GEN_DEFAULT_IS_GOTO_FALLBACK) {
        size_t i,ii;
        OpByte *chkmap;
        bool fallopexists = false;
        unsigned int jcw = codewidth,jaw = addrwidth;

        if (!generic1632) {
            if (flags & OUTCODE_GEN_CODE32)
                jcw = (jcw == 32) ? 16 : 32;
            if (flags & OUTCODE_GEN_ADDR32)
                jaw = (jaw == 32) ? 16 : 32;
        }

        // don't generate GOTO if the fallback doesn't exist
        i=0;
        chkmap = &maproot;
        for (i=0;i < opbaselen;i++) {
            if (chkmap->final && chkmap->isprefix)
                chkmap = &maproot;

            if (chkmap->final) {
                if ((i+1) != opbaselen) chkmap = NULL;
                break;
            }

            if (chkmap->opmap_valid && chkmap->opmap[opbase[i]] != NULL) {
                chkmap = chkmap->opmap[opbase[i]];
            }
            else {
                chkmap = NULL;
                break;
            }
        }
        if (chkmap != NULL)
            fallopexists = true;

        if (fallopexists) {
            // HACK skip prefix we just handled
            i=0;
            if (i < opbaselen && (opbase[i] == 0x66 || opbase[i] == 0x67 || opbase[i] == 0xF2 || opbase[i] == 0xF3)) i++;
            // END HACK
            ii=i;

            if (!generic1632 && generic_case_1632 && (jcw != jaw) && (jcw != codewidth || jaw != addrwidth))
                fprintf(out_fp,"%s        goto _x86decode_begin_code%u_addr%u_opcode_parse_",
                        indent_str.c_str(),32/*FIXME*/,32/*FIXME*/);
            else
                fprintf(out_fp,"%s        goto _x86decode_begin_code%u_addr%u_opcode_parse_",
                        indent_str.c_str(),jcw,jaw);

            for (;i < opbaselen;i++) fprintf(out_fp,"%02X",opbase[i]);
            if (generic1632 || (generic_case_1632 && (jcw != jaw) && (jcw != codewidth || jaw != addrwidth))) fprintf(out_fp,"_generic");
            fprintf(out_fp,"; /* Fall through to normal");
            for (i=ii;i < opbaselen;i++) fprintf(out_fp," 0x%02X",opbase[i]);
            fprintf(out_fp," opcode handling */\n");
        }
        else {
            fprintf(out_fp,"%s        goto _x86decode_illegal_opcode; /* non-mandatory fallback does not exist */\n",indent_str.c_str());
        }
    }
    else
        fprintf(out_fp,"%s        goto _x86decode_illegal_opcode;\n",indent_str.c_str());

    fprintf(out_fp,"%s};\n",indent_str.c_str());

    if (opbaselen != 0) {
        fprintf(out_fp,"/* End of opcodes starting with ");
        for (size_t i=0;i < opbaselen;i++) fprintf(out_fp,"%02Xh ",opbase[i]);
        fprintf(out_fp,"*/\n");
    }

    for (unsigned int op=0;op < 0x100;op++) {
        submap = map.opmap[op];
        if (submap != NULL) submap->already = false;
    }
}

int main(int argc,char **argv) {
    static const uint8_t empty_opcode[1] = {0};
    bool res;

    if (!parse(argc,argv))
        return 1;

    if (in_path == "-")
        in_fp = fdopen(dup(0/*STDIN*/),"r");
    else
        in_fp = fopen(in_path.c_str(),"r");

    res = parse_opcodelist();
    fclose(in_fp);
    if (!res) return 1;

    if (out_path == "-")
        out_fp = fdopen(dup(1/*STDOUT*/),"w");
    else
        out_fp = fopen(out_path.c_str(),"w");

    if (generic1632) {
        fprintf(out_fp,"#define IPFcodeW() (opstate.code32 ? IPFDW() : IPFW())\n");
        fprintf(out_fp,"#define IPFcodeWsigned() (opstate.code32 ? IPFDWsigned() : IPFWsigned())\n");

        fprintf(out_fp,"#define IPFaddrW() (opstate.addr32 ? IPFDW() : IPFW())\n");
        fprintf(out_fp,"#define IPFaddrWsigned() (opstate.addr32 ? IPFDWsigned() : IPFWsigned())\n");
    }
    else {
        if (sel_code_width == 32) {
            fprintf(out_fp,"#define IPFcodeW() IPFDW()\n");
            fprintf(out_fp,"#define IPFcodeWsigned() IPFDWsigned()\n");
        }
        else {
            fprintf(out_fp,"#define IPFcodeW() IPFW()\n");
            fprintf(out_fp,"#define IPFcodeWsigned() IPFWsigned()\n");
        }

        if (sel_addr_width == 32) {
            fprintf(out_fp,"#define IPFaddrW() IPFDW()\n");
            fprintf(out_fp,"#define IPFaddrWsigned() IPFDWsigned()\n");
        }
        else {
            fprintf(out_fp,"#define IPFaddrW() IPFW()\n");
            fprintf(out_fp,"#define IPFaddrWsigned() IPFWsigned()\n");
        }
    }

    /* TODO: 16-bit and 32-bit modes if opcode list says the CPU supports the 32-bit operand overrides.
     *       If that is the case, we generate 16-bit and 32-bit cases in a "figure 8" goto configuration,
     *       so that there is one while() loop for executing 16-bit code and one while() loop for executing 32-bit code,
     *       and opcode prefixes jump from one loop to another. Another plan is to have 4 while loops, one for each
     *       permutation of 16-bit/32-bit code and 16-bit/32-bit addressing.
     *
     *       Sound stupid? Maybe, but the idea is that most code runs in the CPU mode it's designed for and that
     *       the operator overrides are uncommon, therefore the opcode decoding could gain a speedup by not having
     *       conditional jumps per instruction with regard to opcode size and address size decoding. */
    outcode_gen(sel_code_width,sel_addr_width,empty_opcode/*opcode base*/,0/*opcode base len*/,
        sel_code_width == 32 ? opmap32 : opmap16,0/*indent*/,0/*flags*/,sel_code_width == 32 ? opmap32 : opmap16);

    fprintf(out_fp,"#undef IPFaddrW\n");
    fprintf(out_fp,"#undef IPFaddrWsigned\n");
    fprintf(out_fp,"#undef IPFcodeW\n");
    fprintf(out_fp,"#undef IPFcodeWsigned\n");

    fclose(out_fp);

    if (!symbol_path.empty()) {
        vector<size_t> offsetlist;
        size_t count,offset;
        FILE *fp;

        fp = fopen(symbol_path.c_str(),"w");
        if (!fp) {
            fprintf(stderr,"Cannot open symbol file for writing, %s\n",strerror(errno));
            return 1;
        }

        count = 0;
        fprintf(fp,"/* auto-generated */\n");
        fprintf(fp,"enum {\n");
        fprintf(fp,"\tOPCODE_NONE, /* =%lu */\n",(unsigned long)(count++));
        for (map<string,bool>::iterator i=opcode_symbol_list.begin();i!=opcode_symbol_list.end();i++) {
            const string &ent = i->first;

            fprintf(fp,"\tOPCODE_%s, /* =%lu */\n",ent.c_str(),(unsigned long)(count++));
        }
        fprintf(fp,"\tOPCODE__MAX  /* =%lu */\n",(unsigned long)(count++));
        fprintf(fp,"};\n");
        fprintf(fp,"\n");

        count = 0;
        offset = 0;
        offsetlist.clear();
        fprintf(fp,"#ifdef WANT_OPCODE_NAMES\n");
        fprintf(fp,"/* auto-generated */\n");
        fprintf(fp,"const char opcode_names[] = \\\n");
        fprintf(fp,"\t\"\\0\" /* =%lu @%lu */ \\\n",(unsigned long)(count++),(unsigned long)offset);
        offsetlist.push_back(offset);
        offset += 1/*NUL*/;
        for (map<string,bool>::iterator i=opcode_symbol_list.begin();i!=opcode_symbol_list.end();i++) {
            const string &ent = i->first;

            fprintf(fp,"\t\"%s\\0\" /* =%lu @%lu */ \\\n",ent.c_str(),(unsigned long)(count++),(unsigned long)offset);
            offsetlist.push_back(offset);
            offset += 1/*NUL*/ + ent.length();
        }
        fprintf(fp,"\t\"\\0\"; /* =%lu @%lu */\n",(unsigned long)(count++),(unsigned long)offset);
        offsetlist.push_back(offset);
        fprintf(fp,"\n");

        count = 0;
        fprintf(fp,"/* auto-generated */\n");
        fprintf(fp,"const ");
        if (offset >= (size_t)0x10000UL)
            fprintf(fp,"uint32_t ");
        else if (offset >= (size_t)0x100UL)
            fprintf(fp,"uint16_t ");
        else
            fprintf(fp,"uint8_t ");
        fprintf(fp,"opcode_name_offsets[OPCODE__MAX+1] = {\n");
        assert(count < offsetlist.size());
        fprintf(fp,"\t%lu, /* =%lu */\n",(unsigned long)offsetlist[count],(unsigned long)(count)); count++;
        for (map<string,bool>::iterator i=opcode_symbol_list.begin();i!=opcode_symbol_list.end();i++) {
            const string &ent = i->first;

            assert(count < offsetlist.size());
            fprintf(fp,"\t%lu, /* =%lu %s */\n",(unsigned long)offsetlist[count],(unsigned long)(count),ent.c_str()); count++;
        }
        assert(count < offsetlist.size());
        fprintf(fp,"\t%lu  /* =%lu */\n",(unsigned long)offsetlist[count],(unsigned long)(count)); count++;
        fprintf(fp,"};\n");
        fprintf(fp,"#endif /*WANT_OPCODE_NAMES*/\n");
        fprintf(fp,"\n");

        fclose(fp);
    }

    return 0;
}

