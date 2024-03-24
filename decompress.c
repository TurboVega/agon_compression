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

#define COMPRESSION_WINDOW_SIZE 256     // power of 2
#define COMPRESSION_STRING_SIZE 16      // power of 2

typedef void (*WriteDecompressedByte)(void* context, uint8_t);

typedef struct {
    void*               context;
    WriteDecompressedByte write_fcn;
    uint32_t            window_size;
    uint32_t            window_write_index;
    uint32_t            input_count;
    uint32_t            output_count;
    uint8_t             window_data[COMPRESSION_WINDOW_SIZE];
    uint16_t            code;
    uint8_t             code_bits;
} DecompressionData;

void my_write_decompressed_byte(void* context, uint8_t orig_data) {
    fwrite(&orig_data, 1, 1, (FILE*)context);
}

void agon_init_decompression(DecompressionData* dd, void* context, WriteDecompressedByte write_fcn) {
    dd->context = context;
    dd->write_fcn = write_fcn;
    dd->window_size = 0;
    dd->window_write_index = 0;
    dd->input_count = 0;
    dd->output_count = 0;
    dd->code = 0;
    dd->code_bits = 0;
}

void agon_decompress_byte(DecompressionData* dd, uint8_t comp_byte) {
    for (uint8_t bit = 0; bit < 8; bit++) {
        dd->code = (dd->code << 1) | ((comp_byte & 0x80) ? 1 : 0);
        comp_byte <<= 1;
        if (++(dd->code_bits) >= 10) {
            // Interpret the incoming code
            uint16_t command = (dd->code >> 8);
            uint8_t value = (uint8_t)dd->code;
            dd->code = 0;
            dd->code_bits = 0;
            uint8_t size;

            switch (command)
            {
            case 0: // value is copy of original byte
                // Add the new decompressed byte to the window
                dd->window_data[dd->window_write_index++] = value;
                dd->window_write_index &= (COMPRESSION_WINDOW_SIZE - 1);
                if (dd->window_size < COMPRESSION_WINDOW_SIZE) {
                    (dd->window_size)++;
                }
                (*(dd->write_fcn))(dd->context, value);
                dd->output_count++;
                continue;

            case 1: // value is index to string of 4 bytes
                size = 4;
                break;

            case 2: // value is index to string of 8 bytes
                size = 8;
                break;

            case 3: // value is index to string of 16 bytes
                size = 16;
                break;
            }

            // Extract a byte string from the window
            uint8_t string_data[COMPRESSION_STRING_SIZE];
            uint32_t wi = value;
            for (uint8_t si = 0; si < size; si++) {
                uint8_t out_byte = dd->window_data[wi++];
                wi &= (COMPRESSION_WINDOW_SIZE - 1);
                string_data[si] = out_byte;
                (*(dd->write_fcn))(dd->context, out_byte);
                dd->output_count++;
            }
        }
    }
}

int main(int argc, const char** argv) {
    if (argc != 3) {
        printf("Use: decompress <inputfilepath> <outputfilepath>\n");
        return -3;
    }

    printf("Decompressing %s to %s\n", argv[1], argv[2]);
    FILE* fin = fopen(argv[1], "rb");
    if (fin) {
        FILE* fout = fopen(argv[2], "wb");
        if (fout) {
            DecompressionData dd;
            agon_init_decompression(&dd, fout, &my_write_decompressed_byte);
            uint8_t input;
            while (fread(&input, 1, 1, fin) == 1) {
                dd.input_count++;
                agon_decompress_byte(&dd, input);
            }
            fclose(fout);
            fclose(fin);
            uint32_t pct = (dd.output_count * 100) / dd.input_count;
            printf("  Decompressed %u input bytes to %u output bytes (%u%%)\n",
                    dd.input_count, dd.output_count, pct);
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
    return 0;
}
