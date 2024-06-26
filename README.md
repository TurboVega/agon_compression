# agon_compression
This repository has two C applications that compress and decompress files for use with the Agon platform.
The included binaries were compiled with gcc on Linux, but the applications can be built for use on
other platforms (with any small changes that _might_ be necessary), or translated into other languages,
as desired.

Compressed files have an 8-byte header with the following structure:
```
typedef struct {
    uint8_t     marker[3];  // value is "Cmp" (0x43 0x6D 0x70)
    uint8_t     type;       // value is "T" (0x54, meaning TurboVega-style compression)
    uint32_t    orig_size;  // size of the uncompressed data
} CompressionFileHeader;
```

## Compress
This application compresses the input file, yielding the output file. Worst case, the output file
may be 25% larger than the input file. This can only happen if the input file is completely
uncompressible (i.e., has random data not suited for compression). It is generally of no
benefit to compress files that would not normally compress well.

The command-line format is:

compress <i>inputfilepath outputfilepath</i>

The process of compressing data involves converting the input bytes into
a series of output codes, where a single code can represent 1, 4, 8, or 16
input bytes.
This implementation uses a window size of 256 bytes, and a code size of 10 bits.

```
Code bits:
9876543210

00xxxxxxxx   New original data byte with value of xxxxxxxx
01iiiiiiii   String of 4 bytes starting at window index iiiiiiii
10iiiiiiii   String of 8 bytes starting at window index iiiiiiii
11iiiiiiii   String of 16 bytes starting at window index iiiiiiii
```

After compression, the application reports the input and output data sizes. The output
size includes the size of the file header. 

## Decompress
This application decompresses the input file, yielding the output file.

The command-line format is:

decompress <i>inputfilepath outputfilepath</i>

After decompression, the application reports the input and output data sizes. The input
size includes the size of the file header. 

## Verification
It should be the case that compressing a file, then decompressing the
result, yields the same data as the original file. For example, the
following commands should yield a difference of <i>nothing</i>.

```
compress a.bin b.bin
decompress b.bin c.bin
diff a.bin c.bin
```
