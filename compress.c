/*
MIT License

Copyright (c) 2024 Curtis Whitley (TurboVega)

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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// This implementation uses a window size of 256 bytes, and a code size of 10 bits.
// The maximum compressed byte string size is 16 bytes.
//
// Code bits:
// 0000000000
// 9876543210
// ----------
// 00xxxxxxxx   New original data byte with value of xxxxxxxx
// 01iiiiiiii   String of 4 bytes starting at window index iiiiiiii
// 10iiiiiiii   String of 8 bytes starting at window index iiiiiiii
// 11iiiiiiii   String of 16 bytes starting at window index iiiiiiii
//
// Note: Worst case, the output can be 25% LARGER than the input!

#define COMPRESSION_WINDOW_SIZE 256     // power of 2
#define COMPRESSION_STRING_SIZE 16      // power of 2
#define COMPRESSION_TYPE_TURBO  'T'     // TurboVega-style compression

#pragma pack(push, 1)
typedef struct {
    uint8_t     marker[3];
    uint8_t     type;
    uint32_t    orig_size;
} CompressionFileHeader;
#pragma pack(pop)

typedef void (*WriteCompressedByte)(void* context, uint8_t);

typedef struct {
    void*               context;
    WriteCompressedByte write_fcn;
    uint32_t            window_size;
    uint32_t            window_write_index;
    uint32_t            string_size;
    uint32_t            string_read_index;
    uint32_t            string_write_index;
    uint32_t            input_count;
    uint32_t            output_count;
    uint8_t             window_data[COMPRESSION_WINDOW_SIZE];
    uint8_t             string_data[COMPRESSION_STRING_SIZE];
    uint8_t             out_byte;
    uint8_t             out_bits;
} CompressionData;

void my_write_compressed_byte(void* context, uint8_t comp_byte) {
    fwrite(&comp_byte, 1, 1, (FILE*)context);
}

void agon_init_compression(CompressionData* cd, void* context, WriteCompressedByte write_fcn) {
    memset(cd, 0, sizeof(CompressionData));
    cd->context = context;
    cd->write_fcn = write_fcn;
}

void agon_write_compressed_bit(CompressionData* cd, uint8_t comp_bit) {
    cd->out_byte = (cd->out_byte << 1) | comp_bit;
    if (++(cd->out_bits) >= 8) {
        (*cd->write_fcn)(cd->context, cd->out_byte);
        cd->out_byte = 0;
        cd->out_bits = 0;
        cd->output_count++;
    }
}

void agon_write_compressed_byte(CompressionData* cd, uint8_t comp_byte) {
    for (uint8_t bit = 0; bit < 8; bit++) {
        agon_write_compressed_bit(cd, (comp_byte & 0x80) ? 1 : 0);
        comp_byte <<= 1;
    }
}

void agon_compress_byte(CompressionData* cd, uint8_t orig_byte) {
    // Add the new original byte to the string
    cd->string_data[cd->string_write_index++] = orig_byte;
    cd->string_write_index &= (COMPRESSION_STRING_SIZE - 1);
    if (cd->string_size < COMPRESSION_STRING_SIZE) {
        (cd->string_size)++;
    } else {
        cd->string_read_index = (cd->string_read_index + 1) & (COMPRESSION_STRING_SIZE - 1);
    }

    if (cd->string_size >= 16) {
        if (cd->window_size >= 16) {
            // Determine whether the full string of 16 is in the window
            for (uint32_t start = 0; start <= cd->window_size - 16; start++) {
                uint32_t wi = start;
                uint32_t si = cd->string_read_index;
                uint8_t match = 1;
                for (uint8_t i = 0; i < 16; i++) {
                    if (cd->window_data[wi++] != cd->string_data[si++]) {
                        match = 0;
                        break;
                    }
                    wi &= (COMPRESSION_WINDOW_SIZE - 1);
                    si &= (COMPRESSION_STRING_SIZE - 1);
                }
                if (match) {
                    agon_write_compressed_bit(cd, 1); // Output first '1' of '11iiiiiiii'.
                    agon_write_compressed_bit(cd, 1); // Output second '1' of '11iiiiiiii'.
                    agon_write_compressed_byte(cd, (uint8_t) (start)); // Output window index
                    cd->string_size = 0;
                    return;
                }
            }
        }

        if (cd->window_size >= 8) {
            // Determine whether the partial string of 8 is in the window
            for (uint32_t start = 0; start <= cd->window_size - 8; start++) {
                uint32_t wi = start;
                uint32_t si = cd->string_read_index;
                uint8_t match = 1;
                for (uint8_t i = 0; i < 8; i++) {
                    if (cd->window_data[wi++] != cd->string_data[si++]) {
                        match = 0;
                        break;
                    }
                    wi &= (COMPRESSION_WINDOW_SIZE - 1);
                    si &= (COMPRESSION_STRING_SIZE - 1);
                }
                if (match) {
                    agon_write_compressed_bit(cd, 1); // Output '1' of '10iiiiiiii'.
                    agon_write_compressed_bit(cd, 0); // Output '0' of '10iiiiiiii'.
                    agon_write_compressed_byte(cd, (uint8_t) (start)); // Output window index
                    cd->string_size -= 8;
                    cd->string_read_index = (cd->string_read_index + 8) & (COMPRESSION_STRING_SIZE - 1);
                    return;
                }
            }
        }
            
        if (cd->window_size >= 4) {
            // Determine whether the partial string of 4 is in the window
            for (uint32_t start = 0; start <= cd->window_size - 4; start++) {
                uint32_t wi = start;
                uint32_t si = cd->string_read_index;
                uint8_t match = 1;
                for (uint8_t i = 0; i < 4; i++) {
                    if (cd->window_data[wi++] != cd->string_data[si++]) {
                        match = 0;
                        break;
                    }
                    wi &= (COMPRESSION_WINDOW_SIZE - 1);
                    si &= (COMPRESSION_STRING_SIZE - 1);
                }
                if (match) {
                    agon_write_compressed_bit(cd, 0); // Output '0' of '01iiiiiiii'.
                    agon_write_compressed_bit(cd, 1); // Output '1' of '01iiiiiiii'.
                    agon_write_compressed_byte(cd, (uint8_t) (start)); // Output window index
                    cd->string_size -= 4;
                    cd->string_read_index = (cd->string_read_index + 4) & (COMPRESSION_STRING_SIZE - 1);
                    return;
                }
            }
        }

        // Need to make room in the string for the next original byte
        uint8_t old_byte = cd->string_data[cd->string_read_index++];
        agon_write_compressed_bit(cd, 0); // Output '0' of '00xxxxxxxx'.
        agon_write_compressed_bit(cd, 0); // Output '0' of '00xxxxxxxx'.
        agon_write_compressed_byte(cd, (uint8_t) (old_byte)); // Output old original byte
        cd->string_size -= 1;
        cd->string_read_index &= (COMPRESSION_STRING_SIZE - 1);

        // Add the old original byte to the window
        cd->window_data[cd->window_write_index++] = old_byte;
        cd->window_write_index &= (COMPRESSION_WINDOW_SIZE - 1);
        if (cd->window_size < COMPRESSION_WINDOW_SIZE) {
            (cd->window_size)++;
        }
    }
}

void agon_finish_compression(CompressionData* cd) {
    while (cd->string_size) {
        agon_write_compressed_bit(cd, 0); // Output '0' of '00xxxxxxxx'.
        agon_write_compressed_bit(cd, 0); // Output '0' of '00xxxxxxxx'.
        agon_write_compressed_byte(cd, (uint8_t) (cd->string_data[cd->string_read_index++])); // Output orig byte
        cd->string_size -= 1;
        cd->string_read_index &= (COMPRESSION_STRING_SIZE - 1);
    }
    if (cd->out_bits) {
        (*cd->write_fcn)(cd->context, (cd->out_byte << (8 - cd->out_bits))); // Output final bits
        cd->output_count++;
    }
}

int main(int argc, const char** argv) {
    if (argc != 3) {
        printf("Use: compress <inputfilepath> <outputfilepath>\r\n");
        return -3;
    }

    printf("Compressing %s to %s\r\n", argv[1], argv[2]);
    FILE* fin = fopen(argv[1], "rb");
    if (fin) {
        fseek(fin, 0, SEEK_END);
        long orig_size = ftell(fin);
        fseek(fin, 0, SEEK_SET);

        FILE* fout = fopen(argv[2], "wb");
        if (fout) {
            CompressionData cd;
            agon_init_compression(&cd, fout, &my_write_compressed_byte);

            CompressionFileHeader hdr;
            hdr.marker[0] = 'C';
            hdr.marker[1] = 'm';
            hdr.marker[2] = 'p';
            hdr.type = COMPRESSION_TYPE_TURBO;
            hdr.orig_size = (uint32_t) orig_size;
            fwrite(&hdr, sizeof(hdr), 1, fout);
            cd.output_count = sizeof(hdr);

            uint8_t input;
            while (fread(&input, 1, 1, fin) == 1) {
                cd.input_count++;
                agon_compress_byte(&cd, input);
            }
            agon_finish_compression(&cd);
            fclose(fout);
            fclose(fin);
            uint32_t pct = (cd.output_count * 100) / cd.input_count;
            printf("  Compressed %u input bytes to %u output bytes (%u%%)\r\n",
                    cd.input_count, cd.output_count, pct);
            return 0;
        } else {
            fclose(fin);
            printf("Cannot open %s", argv[2]);
            return -2;
        }
    } else {
        printf("Cannot open %s", argv[1]);
        return -1;
    }
}
