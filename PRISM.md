# Prism on wheelcc

A working native-code compiler: Prism source -> x86-64 assembly.

## Build

    cd bin
    ./configure.sh
    ./make.sh

Produces `bin/wheelcc` (the binary keeps wheelcc's name; only the frontend changed).

## Compile a Prism program

The driver script is still set up for C, so call the binary directly:

    cd demo
    ../bin/wheelcc 0 0 0 hello.pr "" "" ""     # -> hello.s
    gcc -c hello.s -o hello.o
    gcc hello.o -o hello
    ./hello; echo $?

Argument order is: DEBUG_ENUM OPTIM_L1_MASK OPTIM_L2_ENUM FILE LIBC_DIR SOURCE_DIR INCLUDE_DIRS
(the file argument drops the extension in driver.sh, but passing the full name works).

Debug codes: 254 dumps the C AST, 253 adds the symbol tables, 252 dumps TAC.

## What changed from stock wheelcc

Replaced (2 of 24 source files):
  src/frontend/parser/lexer.c   -> src/frontend/parser/prism_lexer.c
  src/frontend/parser/parser.c  -> src/frontend/parser/prism_parser.c

Edited:
  src/main.c          - lex_c_code + parse_tokens replaced by one parse_prism call,
                        plus a read_prism_source helper
  build/build.sh:51-52  - source list swapped (this is the list make.sh actually uses)
  build/CMakeLists.txt:43-44 - same swap for the cmake path

Untouched (22 files, ~22,000 lines): semantic.c, tac_repr.c, optim_tac.c,
reg_alloc.c, asm_gen.c, stack_fix.c, symt_cvt.c, gas_code.c, all of ast/, all of util/.

## Language

    struct Point { x: i32, y: i32, };
    union Reg { raw: u64, lo: u32, };

    static var counter: u64 = 0;
    extern fun outb(port: u32, value: u32);

    fun add(a: i32, b: i32) -> i32 { return a + b; }

Types:  i8 u8 i32 u32 i64 u64 char bool void, *T, [N]T, struct Foo, union Bar
        (i16/u16 error out: AST_Short_t does not exist yet)
Casts:  expr as *u8
Control: if/else while do-while for switch/case/default break continue goto + labels
Ops:    + - * / % & | ^ ~ << >> ! == != < <= > >= and or && ||
        ++ -- and compound assignment (+= -= *= /= %= &= |= ^= <<= >>=)
        ?: sizeof(T) sizeof expr
Literals: 42 42u 42L 42UL 0xFF 0b1010 'a' "str" nil true false
        (no floats - they reach CConstDouble, which means xmm, which faults
         in kernel context before SSE is enabled)

## Verified

demo/hello.pr  -> returns 42
demo/kernel.pr -> returns 7, exercises structs, arrays, pointers, sizeof,
                  switch/goto/labels, do-while, ternary, ++/--, compound assign

## Next, for bare metal

1. bin/driver.sh:575 - swap the ld line for: -nostdlib -static -no-pie -T kernel.ld
   and delete the ${CC} fallback block below it
2. bin/crt.s - delete; replace with your kernel entry stub
3. Pin kernel builds to -O1 (no volatile yet, and -O2+ runs the two passes that
   would delete MMIO writes)
4. Packed structs: semantic.c:2543 and :2561 are the padding sites
5. Section/alignment directives: gas_code.c:849-880 and :1008
6. Later: inline asm, function pointers, AST_Short_t
