/*
    MIT License

    Copyright (c) 2026 tonycons (Anthony Constantinescu)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <sys/mman.h>
#include <unistd.h>


struct
{
    char* src;
    size_t src_pos;
    uint8_t* exec_buf;
    size_t exec_size;
    size_t exec_pos;
    size_t scope;
} ctx;


void gen(char op, uint8_t* out_asm, size_t* out_asm_pos, size_t* out_src_pos)
{
    assert(out_asm != NULL);
    assert(out_src_pos != NULL);
    assert(out_asm_pos != NULL);

    switch (op) {
    // Increment the data pointer by one (to point to the next cell to the right).
    case '>': {
        static uint8_t const code[] = {
            0x48,
            0xff,
            0xc6,  // inc rsi
        };
        memcpy(out_asm, code, sizeof(code));
        *out_asm_pos += sizeof(code);
        break;
    }
    // Decrement the data pointer by one (to point to the next cell to the left). Undefined if at 0.
    case '<': {
        static uint8_t const code[] = {
            0x48,
            0xff,
            0xce,  // dec rsi
        };
        memcpy(out_asm, code, sizeof(code));
        *out_asm_pos += sizeof(code);
        break;
    }
    // Increment the byte at the data pointer by one modulo 256.
    case '+': {
        size_t count = 0;
        for (size_t i = 0; ctx.src[*out_src_pos + i] == '+'; i++) {
            ++count;
        }
        if (count == 1) {

            static uint8_t const code[] = {
                0xfe,
                0x06,  // inc byte [rsi]
            };

            memcpy(out_asm, code, sizeof(code));
            *out_asm_pos += sizeof(code);
        } else {

            uint8_t code[] = {
                0x80,
                0x06,
                (uint8_t)count,  // add byte [rsi], count
            };

            memcpy(out_asm, code, sizeof(code));
            *out_asm_pos += sizeof(code);
        }
        (*out_src_pos) += count;
        return;
    }
    // Decrement the byte at the data pointer by one modulo 256.
    case '-': {
        size_t count = 0;
        for (size_t i = 0; ctx.src[*out_src_pos + i] == '-'; i++) {
            ++count;
        }
        if (count == 1) {
            static uint8_t const code[] = {
                0xfe,
                0x0e,  // dec byte [rsi]
            };
            memcpy(out_asm, code, sizeof(code));
            *out_asm_pos += sizeof(code);
        } else {
            uint8_t code[] = {
                0x80,
                0x2e,
                (uint8_t)count,  // sub byte [rsi], count
            };
            memcpy(out_asm, code, sizeof(code));
            *out_asm_pos += sizeof(code);
        }
        (*out_src_pos) += count;
        return;
    }
    // Output the byte at the data pointer.
    case '.': {
        static uint8_t const code[] = {
            0xbf, 0x01, 0x00, 0x00, 0x00,  // mov edi, 1
            0xb8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1
            0x0f, 0x05,                    // syscall
        };
        memcpy(out_asm, code, sizeof(code));
        *out_asm_pos += sizeof(code);
        break;
    }
    // Accept one byte of input, storing its value in the byte at the data pointer.
    case ',': {
        static uint8_t const code[] = {
            0x31, 0xc0,  // xor eax, eax
            0x31, 0xff,  // xor edi, edi
            0x0f, 0x05,  // syscall
        };
        memcpy(out_asm, code, sizeof(code));
        *out_asm_pos += sizeof(code);
        break;
    }
    // If the byte at the data pointer is zero, then instead of moving the instruction pointer forward to the next
    // command, jump it forward to the command after the matching ] command.
    case '[': {
        size_t byte_offset = 9;
        size_t src_offset = 0;
        size_t depth = ctx.scope;

        ctx.scope++;

        // find the offset to the matching ]

        for (src_offset = 1; depth != ctx.scope;)  //
        {
            assert(ctx.src[*out_src_pos + src_offset] != '\0');
            switch (ctx.src[*out_src_pos + src_offset]) {
            case '>':
            case '<': byte_offset += 3; break;
            case '-':
            case '+': {
                char ch = ctx.src[*out_src_pos + src_offset];
                size_t n = 0;
                while (ctx.src[*out_src_pos + src_offset + n] == ch) {
                    ++n;
                }
                byte_offset += n == 1 ? 2 : 3;
                src_offset += n;
                continue;
            }
            case '.': byte_offset += 12; break;
            case ',': byte_offset += 6; break;
            case '[':
                byte_offset += 9;
                ctx.scope++;
                break;
            case ']':
                byte_offset += 9;
                ctx.scope--;
                break;
            }
            src_offset += 1;
        }
        assert(byte_offset != 0);
        assert(src_offset != 0);

        // printf("jump ahead %zu positions & %zu bytes\n", src_offset, byte_offset);
        // printf("begin of loop: %d\n", (int32_t)(*out_asm_pos + byte_offset) - ((int32_t)byte_offset));

        union {
            int32_t x;
            uint8_t b[4];
        } u = {.x = byte_offset - 9};
        union {
            int32_t x;
            uint8_t b[4];
        } v = {.x = -((int32_t)byte_offset) + 9};

        uint8_t start_loop_code[] = {
            0x80, 0x3e, 0x00,                           // cmp byte [rsi], 0
            0x0f, 0x84, u.b[0], u.b[1], u.b[2], u.b[3]  // jz [rip+offset]     (near jump)
        };
        uint8_t end_loop_code[] = {
            0x80, 0x3e, 0x00,                           // cmp byte [rsi], 0
            0x0f, 0x85, v.b[0], v.b[1], v.b[2], v.b[3]  // jnz [rip + offset]
        };

        memcpy(out_asm, start_loop_code, sizeof(start_loop_code));
        memcpy(out_asm + byte_offset - 9, end_loop_code, sizeof(end_loop_code));
        *out_asm_pos += 9;
        break;
    }

    // If the byte at the data pointer is nonzero, then instead of moving the instruction pointer forward to the
    // next command, jump it back to the command after the matching [ command.
    case ']': {
        *out_asm_pos += 9;
        ctx.scope--;
        break;
    }
    }
    (*out_src_pos)++;
}


void jit()
{
    ctx.exec_buf = memalign(sysconf(_SC_PAGESIZE), ctx.exec_size = 16000000);

    /*
     Fill the executable memory with the "halt" instruction ( F4 ).
     This is the default value for any part of the executable memory that does not contain code.
     -- it's a safety measure to make the CPU trap if it jumps outside of the compiled code.
     */
    memset(ctx.exec_buf, 0xf4, ctx.exec_size);

    /*
     Now make the executable memory actually executable
    */
    if (mprotect(ctx.exec_buf, ctx.exec_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("Error creating executable memory");
        return;
    }
    /**
     Insert initialization code
     Special registers:
        - rsi: data pointer. Using rsi as the data pointer means that for read and write syscalls, it's already using
     the right register, so there's one less instruction.
    */
    ctx.exec_pos = 0;
    // clang-format off
    uint8_t init_code[] = {
        0x48, 0xc7, 0xc0, 0x00, 0x80, 0x00, 0x00,           // mov rax, 0x8000
        0x48, 0x29, 0xc4,                                   // sub rsp, rax
        0x48, 0x83, 0xe8, 0x08,                             // sub rax, 8
        0x48, 0xc7, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,     // loop: mov qword [rsp+rax], 0
        0x48,0x83,0xe8,0x08,                                // sub rax, 8
        0x48,0x85,0xc0,                                     // test rax, rax
        0x75,0xef,                                          // jnz loop
        0xba,0x01,0x00,0x00,0x00,                           // mov edx, 1 (edx=1 will never change for syscalls)
        0x48,0x89,0xe6,                                     // mov rsi, rsp
    };
    // clang-format on
    memcpy(ctx.exec_buf, init_code, sizeof(init_code));
    ctx.exec_pos += sizeof(init_code);
    ctx.scope = 0;

    // Generate code
    for (ctx.src_pos = 0; ctx.src[ctx.src_pos] != '\0';) {
        gen(ctx.src[ctx.src_pos], ctx.exec_buf + ctx.exec_pos, &ctx.exec_pos, &ctx.src_pos);
    }

    // Insert final code
    uint8_t code[] = {
        0x48, 0x81, 0xc4, 0x00, 0x80, 0x00, 0x00,  // add rsp, 0x8000  (restore stack)
        0x31, 0xff,                                // xor edi, edi
        0xb8, 0x3c, 0x00, 0x00, 0x00,              // mov eax, 60
        0x0f, 0x05                                 // syscall
    };
    memcpy(ctx.exec_buf + ctx.exec_pos, code, sizeof(code));
    ctx.exec_pos += sizeof(code);

    /**
     Execute the code
    */
    ((void (*)(void))ctx.exec_buf)();

    // cleanupp
    free(ctx.exec_buf);
}


void usage()
{
    puts(
        "bfc usage:\n"
        "   1)  Input source code in shell:                     bfc \n"
        "   2)  Run the JIT on a source file:                   bfc <FILENAME> \n"
        "   3)  Produce an ELF executable from a source file:   bfc -c <SOURCE_FILENAME> -o <OUTPUT_FILENAME> \n");
}


int main(int argc, char** argv)
{
    if (argc == 1) {
        size_t _;
        getline(&ctx.src, &_, stdin);
        jit();
        free(ctx.src);
    }
    if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage();
            exit(0);
        }
        char* filename = argv[1];
        FILE* f = fopen(filename, "rb");
        if (!f) {
            fprintf(stderr, "Unable to read source file: %s\n", filename);
            exit(-1);
        }
        fseek(f, 0, SEEK_END);
        long length = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* buffer = malloc(length + 1);
        if (buffer) {
            fread(buffer, 1, length, f);
            buffer[length] = '\0';
        }
        fclose(f);
        ctx.src = buffer;
        jit();
        free(ctx.src);
    }
    if (argc == 3 || argc == 4) {
        usage();
        exit(-1);
    }
    if (argc == 5) {
        if (strcmp(argv[1], "-c") != 0 || strcmp(argv[3], "-o") != 0) {
            usage();
            exit(-1);
        }
        printf("Generating an ELF file is not yet implemented!\n");
    }
    return 0;

    // ctx.src =

    // // io test
    // //",.";

    // // memory initialization test: should print 'a'
    // //"+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++.";

    // // forward jump test -- works
    // //"[+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]+++++++++++"
    // //"+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++.";

    // // loop test (should add two input chars)
    // //",>,<[->+<]>.";

    // // Hello world
    // "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.";
}
