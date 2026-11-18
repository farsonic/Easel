/*
 * webp.c - WebP pictures to RGBA (see amigafmt.h): lossy VP8 key frames (RFC 6386, with segments,
 * up to eight token partitions and both loop filters), lossless VP8L (all four transforms, the
 * colour cache and meta Huffman codes), the extended VP8X form with an ALPH alpha plane (raw or
 * VP8L-compressed, with its three prediction filters), and the first frame of an animation laid on
 * its canvas as libwebp's animation decoder lays it.
 *
 * The decoding follows libwebp's integer arithmetic throughout - its boolean decoder, coefficient
 * parsing, dequantization, the inverse transforms, the loop filters, the "fancy" chroma upsampler
 * and its 14-bit YUV to RGB - so a lossy picture comes out pixel for pixel as libwebp decodes it.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the VP8 constant tables (crates/codecs/src/codecs/vp8/tables.rs: token and mode probabilities,
 * quantizer steps, bands, zig-zag, the sub-block mode tree), the inverse DCT and WHT
 * (vp8/transform.rs) and the 16x16, chroma and 4x4 intra predictors (vp8/predict.rs). The RIFF
 * chunk walk follows PhotoCraft's webp.rs container code.
 *
 * The rest is written from RFC 6386 (VP8), RFC 9649 (WebP: container, VP8L, ALPH, ANMF). To match
 * libwebp's output exactly it follows libwebp's documented integer arithmetic (the bool decoder's
 * range form, dequantization rounding, filter-strength computation, the "fancy" upsampler and the
 * 14-bit YUV to RGB constants): libwebp, Copyright (c) 2010 Google Inc., BSD-3-Clause license.
 *
 * Hostile input: every chunk length and partition size is checked against the data, the boolean
 * decoders read zeros (then stop) past their partitions, every Huffman code is checked complete,
 * and every back-reference against the pixels decoded so far. Nothing big goes on the stack.
 * All numbers in the file are little-endian and are read a byte at a time.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "amigafmt.h"
#include "xwrite.h"                     /* (1.2: webp_encode_lossy) */

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le24(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16); }
static u32 le32(const u8 *p) { return le24(p) | ((u32)p[3] << 24); }

/* an i16 as a two's-complement 16-bit store would leave it (libwebp keeps coefficients in int16) */
static i16 wrap16(i32 v) { v &= 0xFFFF; return (i16)(v >= 32768 ? v - 65536 : v); }

/* ================================================================== VP8 tables (from PhotoCraft) */
static const u8 default_coeff_probs[1056] = {
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 253, 136, 254, 255, 228, 219, 128, 128, 128,
  128, 128, 189, 129, 242, 255, 227, 213, 255, 219, 128, 128, 128, 106, 126, 227, 252, 214, 209, 255, 255,
  128, 128, 128, 1, 98, 248, 255, 236, 226, 255, 255, 128, 128, 128, 181, 133, 238, 254, 221, 234, 255, 154,
  128, 128, 128, 78, 134, 202, 247, 198, 180, 255, 219, 128, 128, 128, 1, 185, 249, 255, 243, 255, 128, 128,
  128, 128, 128, 184, 150, 247, 255, 236, 224, 128, 128, 128, 128, 128, 77, 110, 216, 255, 236, 230, 128, 128,
  128, 128, 128, 1, 101, 251, 255, 241, 255, 128, 128, 128, 128, 128, 170, 139, 241, 252, 236, 209, 255, 255,
  128, 128, 128, 37, 116, 196, 243, 228, 255, 255, 255, 128, 128, 128, 1, 204, 254, 255, 245, 255, 128, 128,
  128, 128, 128, 207, 160, 250, 255, 238, 128, 128, 128, 128, 128, 128, 102, 103, 231, 255, 211, 171, 128,
  128, 128, 128, 128, 1, 152, 252, 255, 240, 255, 128, 128, 128, 128, 128, 177, 135, 243, 255, 234, 225, 128,
  128, 128, 128, 128, 80, 129, 211, 255, 194, 224, 128, 128, 128, 128, 128, 1, 1, 255, 128, 128, 128, 128,
  128, 128, 128, 128, 246, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128, 255, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 198, 35, 237, 223, 193, 187, 162, 160, 145, 155, 62, 131, 45, 198, 221, 172, 176, 220,
  157, 252, 221, 1, 68, 47, 146, 208, 149, 167, 221, 162, 255, 223, 128, 1, 149, 241, 255, 221, 224, 255, 255,
  128, 128, 128, 184, 141, 234, 253, 222, 220, 255, 199, 128, 128, 128, 81, 99, 181, 242, 176, 190, 249, 202,
  255, 255, 128, 1, 129, 232, 253, 214, 197, 242, 196, 255, 255, 128, 99, 121, 210, 250, 201, 198, 255, 202,
  128, 128, 128, 23, 91, 163, 242, 170, 187, 247, 210, 255, 255, 128, 1, 200, 246, 255, 234, 255, 128, 128,
  128, 128, 128, 109, 178, 241, 255, 231, 245, 255, 255, 128, 128, 128, 44, 130, 201, 253, 205, 192, 255, 255,
  128, 128, 128, 1, 132, 239, 251, 219, 209, 255, 165, 128, 128, 128, 94, 136, 225, 251, 218, 190, 255, 255,
  128, 128, 128, 22, 100, 174, 245, 186, 161, 255, 199, 128, 128, 128, 1, 182, 249, 255, 232, 235, 128, 128,
  128, 128, 128, 124, 143, 241, 255, 227, 234, 128, 128, 128, 128, 128, 35, 77, 181, 251, 193, 211, 255, 205,
  128, 128, 128, 1, 157, 247, 255, 236, 231, 255, 255, 128, 128, 128, 121, 141, 235, 255, 225, 227, 255, 255,
  128, 128, 128, 45, 99, 188, 251, 195, 217, 255, 224, 128, 128, 128, 1, 1, 251, 255, 213, 255, 128, 128, 128,
  128, 128, 203, 1, 248, 255, 255, 128, 128, 128, 128, 128, 128, 137, 1, 177, 255, 224, 255, 128, 128, 128,
  128, 128, 253, 9, 248, 251, 207, 208, 255, 192, 128, 128, 128, 175, 13, 224, 243, 193, 185, 249, 198, 255,
  255, 128, 73, 17, 171, 221, 161, 179, 236, 167, 255, 234, 128, 1, 95, 247, 253, 212, 183, 255, 255, 128,
  128, 128, 239, 90, 244, 250, 211, 209, 255, 255, 128, 128, 128, 155, 77, 195, 248, 188, 195, 255, 255, 128,
  128, 128, 1, 24, 239, 251, 218, 219, 255, 205, 128, 128, 128, 201, 51, 219, 255, 196, 186, 128, 128, 128,
  128, 128, 69, 46, 190, 239, 201, 218, 255, 228, 128, 128, 128, 1, 191, 251, 255, 255, 128, 128, 128, 128,
  128, 128, 223, 165, 249, 255, 213, 255, 128, 128, 128, 128, 128, 141, 124, 248, 255, 255, 128, 128, 128,
  128, 128, 128, 1, 16, 248, 255, 255, 128, 128, 128, 128, 128, 128, 190, 36, 230, 255, 236, 255, 128, 128,
  128, 128, 128, 149, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128, 1, 226, 255, 128, 128, 128, 128, 128,
  128, 128, 128, 247, 192, 255, 128, 128, 128, 128, 128, 128, 128, 128, 240, 128, 255, 128, 128, 128, 128,
  128, 128, 128, 128, 1, 134, 252, 255, 255, 128, 128, 128, 128, 128, 128, 213, 62, 250, 255, 255, 128, 128,
  128, 128, 128, 128, 55, 93, 255, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 202, 24, 213, 235, 186, 191, 220, 160, 240, 175, 255, 126, 38, 182, 232, 169, 184,
  228, 174, 255, 187, 128, 61, 46, 138, 219, 151, 178, 240, 170, 255, 216, 128, 1, 112, 230, 250, 199, 191,
  247, 159, 255, 255, 128, 166, 109, 228, 252, 211, 215, 255, 174, 128, 128, 128, 39, 77, 162, 232, 172, 180,
  245, 178, 255, 255, 128, 1, 52, 220, 246, 198, 199, 249, 220, 255, 255, 128, 124, 74, 191, 243, 183, 193,
  250, 221, 255, 255, 128, 24, 71, 130, 219, 154, 170, 243, 182, 255, 255, 128, 1, 182, 225, 249, 219, 240,
  255, 224, 128, 128, 128, 149, 150, 226, 252, 216, 205, 255, 171, 128, 128, 128, 28, 108, 170, 242, 183, 194,
  254, 223, 255, 255, 128, 1, 81, 230, 252, 204, 203, 255, 192, 128, 128, 128, 123, 102, 209, 247, 188, 196,
  255, 233, 128, 128, 128, 20, 95, 153, 243, 164, 173, 255, 203, 128, 128, 128, 1, 222, 248, 255, 216, 213,
  128, 128, 128, 128, 128, 168, 175, 246, 252, 235, 205, 255, 255, 128, 128, 128, 47, 116, 215, 255, 211, 212,
  255, 255, 128, 128, 128, 1, 121, 236, 253, 212, 214, 255, 255, 128, 128, 128, 141, 84, 213, 252, 201, 202,
  255, 219, 128, 128, 128, 42, 80, 160, 240, 162, 185, 255, 205, 128, 128, 128, 1, 1, 255, 128, 128, 128, 128,
  128, 128, 128, 128, 244, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128, 238, 1, 255, 128, 128, 128, 128,
  128, 128, 128, 128
};
static const u8 coeff_update_probs[1056] = {
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 176, 246, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 223, 241, 252, 255, 255, 255, 255, 255, 255, 255, 255, 249, 253, 253, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 244, 252, 255, 255, 255, 255, 255, 255, 255, 255, 234, 254, 254, 255, 255, 255, 255,
  255, 255, 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 246, 254, 255, 255, 255,
  255, 255, 255, 255, 255, 239, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255, 254, 255, 254, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255, 251, 255, 254, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 253, 254,
  255, 255, 255, 255, 255, 255, 255, 255, 251, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 254, 255,
  254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 253, 255, 254, 255, 255, 255, 255, 255, 255, 250,
  255, 254, 255, 254, 255, 255, 255, 255, 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 217, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 225, 252, 241, 253, 255, 255, 254, 255, 255, 255, 255, 234, 250, 241, 250, 253, 255, 253, 254,
  255, 255, 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 223, 254, 254, 255, 255, 255, 255,
  255, 255, 255, 255, 238, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 248, 254, 255, 255, 255,
  255, 255, 255, 255, 255, 249, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 247, 254, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 253, 254,
  255, 255, 255, 255, 255, 255, 255, 255, 252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 253,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255, 250, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 186, 251, 250, 255, 255, 255, 255, 255, 255, 255, 255, 234, 251, 244, 254, 255, 255, 255,
  255, 255, 255, 255, 251, 251, 243, 253, 254, 255, 254, 255, 255, 255, 255, 255, 253, 254, 255, 255, 255,
  255, 255, 255, 255, 255, 236, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255, 251, 253, 253, 254, 254,
  255, 255, 255, 255, 255, 255, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 254, 254, 254, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 248, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 250, 254, 252, 254, 255, 255, 255, 255, 255, 255, 255, 248, 254, 249, 253, 255,
  255, 255, 255, 255, 255, 255, 255, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255, 246, 253, 253, 255,
  255, 255, 255, 255, 255, 255, 255, 252, 254, 251, 254, 254, 255, 255, 255, 255, 255, 255, 255, 254, 252,
  255, 255, 255, 255, 255, 255, 255, 255, 248, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255, 253, 255,
  254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255, 245,
  251, 254, 255, 255, 255, 255, 255, 255, 255, 255, 253, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 251, 253, 255, 255, 255, 255, 255, 255, 255, 255, 252, 253, 254, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 252, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 249, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 250, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255
};
static const u8 dc_qlookup[128] = {
  4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 17, 18, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 25,
  25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
  76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 91, 93, 95, 96, 98, 100, 101, 102, 104, 106, 108,
  110, 112, 114, 116, 118, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 143, 145, 148, 151, 154, 157
};
static const u16 ac_qlookup[128] = {
  4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
  60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108,
  110, 112, 114, 116, 119, 122, 125, 128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 161, 164, 167,
  170, 173, 177, 181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 234, 239, 245, 249, 254,
  259, 264, 269, 274, 279, 284
};
static const u8 kf_bmode_probs[900] = {
  231, 120, 48, 89, 115, 113, 120, 152, 112, 152, 179, 64, 126, 170, 118, 46, 70, 95, 175, 69, 143, 80, 85,
  82, 72, 155, 103, 56, 58, 10, 171, 218, 189, 17, 13, 152, 144, 71, 10, 38, 171, 213, 144, 34, 26, 114, 26,
  17, 163, 44, 195, 21, 10, 173, 121, 24, 80, 195, 26, 62, 44, 64, 85, 170, 46, 55, 19, 136, 160, 33, 206, 71,
  63, 20, 8, 114, 114, 208, 12, 9, 226, 81, 40, 11, 96, 182, 84, 29, 16, 36, 134, 183, 89, 137, 98, 101, 106,
  165, 148, 72, 187, 100, 130, 157, 111, 32, 75, 80, 66, 102, 167, 99, 74, 62, 40, 234, 128, 41, 53, 9, 178,
  241, 141, 26, 8, 107, 104, 79, 12, 27, 217, 255, 87, 17, 7, 74, 43, 26, 146, 73, 166, 49, 23, 157, 65, 38,
  105, 160, 51, 52, 31, 115, 128, 87, 68, 71, 44, 114, 51, 15, 186, 23, 47, 41, 14, 110, 182, 183, 21, 17,
  194, 66, 45, 25, 102, 197, 189, 23, 18, 22, 88, 88, 147, 150, 42, 46, 45, 196, 205, 43, 97, 183, 117, 85,
  38, 35, 179, 61, 39, 53, 200, 87, 26, 21, 43, 232, 171, 56, 34, 51, 104, 114, 102, 29, 93, 77, 107, 54, 32,
  26, 51, 1, 81, 43, 31, 39, 28, 85, 171, 58, 165, 90, 98, 64, 34, 22, 116, 206, 23, 34, 43, 166, 73, 68, 25,
  106, 22, 64, 171, 36, 225, 114, 34, 19, 21, 102, 132, 188, 16, 76, 124, 62, 18, 78, 95, 85, 57, 50, 48, 51,
  193, 101, 35, 159, 215, 111, 89, 46, 111, 60, 148, 31, 172, 219, 228, 21, 18, 111, 112, 113, 77, 85, 179,
  255, 38, 120, 114, 40, 42, 1, 196, 245, 209, 10, 25, 109, 100, 80, 8, 43, 154, 1, 51, 26, 71, 88, 43, 29,
  140, 166, 213, 37, 43, 154, 61, 63, 30, 155, 67, 45, 68, 1, 209, 142, 78, 78, 16, 255, 128, 34, 197, 171,
  41, 40, 5, 102, 211, 183, 4, 1, 221, 51, 50, 17, 168, 209, 192, 23, 25, 82, 125, 98, 42, 88, 104, 85, 117,
  175, 82, 95, 84, 53, 89, 128, 100, 113, 101, 45, 75, 79, 123, 47, 51, 128, 81, 171, 1, 57, 17, 5, 71, 102,
  57, 53, 41, 49, 115, 21, 2, 10, 102, 255, 166, 23, 6, 38, 33, 13, 121, 57, 73, 26, 1, 85, 41, 10, 67, 138,
  77, 110, 90, 47, 114, 101, 29, 16, 10, 85, 128, 101, 196, 26, 57, 18, 10, 102, 102, 213, 34, 20, 43, 117,
  20, 15, 36, 163, 128, 68, 1, 26, 138, 31, 36, 171, 27, 166, 38, 44, 229, 67, 87, 58, 169, 82, 115, 26, 59,
  179, 63, 59, 90, 180, 59, 166, 93, 73, 154, 40, 40, 21, 116, 143, 209, 34, 39, 175, 57, 46, 22, 24, 128, 1,
  54, 17, 37, 47, 15, 16, 183, 34, 223, 49, 45, 183, 46, 17, 33, 183, 6, 98, 15, 32, 183, 65, 32, 73, 115, 28,
  128, 23, 128, 205, 40, 3, 9, 115, 51, 192, 18, 6, 223, 87, 37, 9, 115, 59, 77, 64, 21, 47, 104, 55, 44, 218,
  9, 54, 53, 130, 226, 64, 90, 70, 205, 40, 41, 23, 26, 57, 54, 57, 112, 184, 5, 41, 38, 166, 213, 30, 34, 26,
  133, 152, 116, 10, 32, 134, 75, 32, 12, 51, 192, 255, 160, 43, 51, 39, 19, 53, 221, 26, 114, 32, 73, 255,
  31, 9, 65, 234, 2, 15, 1, 118, 73, 88, 31, 35, 67, 102, 85, 55, 186, 85, 56, 21, 23, 111, 59, 205, 45, 37,
  192, 55, 38, 70, 124, 73, 102, 1, 34, 98, 102, 61, 71, 37, 34, 53, 31, 243, 192, 69, 60, 71, 38, 73, 119,
  28, 222, 37, 68, 45, 128, 34, 1, 47, 11, 245, 171, 62, 17, 19, 70, 146, 85, 55, 62, 70, 75, 15, 9, 9, 64,
  255, 184, 119, 16, 37, 43, 37, 154, 100, 163, 85, 160, 1, 63, 9, 92, 136, 28, 64, 32, 201, 85, 86, 6, 28, 5,
  64, 255, 25, 248, 1, 56, 8, 17, 132, 137, 255, 55, 116, 128, 58, 15, 20, 82, 135, 57, 26, 121, 40, 164, 50,
  31, 137, 154, 133, 25, 35, 218, 51, 103, 44, 131, 131, 123, 31, 6, 158, 86, 40, 64, 135, 148, 224, 45, 183,
  128, 22, 26, 17, 131, 240, 154, 14, 1, 209, 83, 12, 13, 54, 192, 255, 68, 47, 28, 45, 16, 21, 91, 64, 222,
  7, 1, 197, 56, 21, 39, 155, 60, 138, 23, 102, 213, 85, 26, 85, 85, 128, 128, 32, 146, 171, 18, 11, 7, 63,
  144, 171, 4, 4, 246, 35, 27, 10, 146, 174, 171, 12, 26, 128, 190, 80, 35, 99, 180, 80, 126, 54, 45, 85, 126,
  47, 87, 176, 51, 41, 20, 32, 101, 75, 128, 139, 118, 146, 116, 128, 85, 56, 41, 15, 176, 236, 85, 37, 9, 62,
  146, 36, 19, 30, 171, 255, 97, 27, 20, 71, 30, 17, 119, 118, 255, 17, 18, 138, 101, 38, 60, 138, 55, 70, 43,
  26, 142, 138, 45, 61, 62, 219, 1, 81, 188, 64, 32, 41, 20, 117, 151, 142, 20, 21, 163, 112, 19, 12, 61, 195,
  128, 48, 4, 24
};
/* coefficient position -> probability band, with a sentinel for position 16 */
static const u8 coeff_bands[17] = { 0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0 };
static const u8 zigzag[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };
/* extra-bit probabilities of token categories 3 to 6 (0-terminated) */
static const u8 cat3[] = { 173, 148, 140, 0 }, cat4[] = { 176, 155, 140, 135, 0 }, cat5[] = { 180, 157, 141, 134, 130, 0 };
static const u8 cat6[] = { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 };
static const u8 *const cat3456[4] = { cat3, cat4, cat5, cat6 };

/* sub-block modes in the RFC's order; the 16x16 and chroma modes are kept as the sub-block mode
   that stands for them as context (DC, V = VE, H = HE, TM) */
enum { B_DC, B_TM, B_VE, B_HE, B_LD, B_RD, B_VR, B_VL, B_HD, B_HU };
/* bmode_tree (RFC 6386 11.2): positive entries index the next node pair, others are -leaf */
static const signed char bmode_tree[18] = {
  -B_DC, 2, -B_TM, 4, -B_VE, 6, 8, 12, -B_HE, 10, -B_RD, -B_VR, -B_LD, 14, -B_VL, 16, -B_HD, -B_HU
};

/* ================================================================== VP8: the boolean decoder */
/* libwebp's form of RFC 6386 section 7: range holds the range less one, value the unread bits
   (bits of them below the decision point). Past the end it reads one byte of zeros and sets eof */
typedef struct { const u8 *p, *end; u32 value, range; int bits, eof; } BoolDec;

static void bd_load(BoolDec *b)
{
  if (b->p < b->end) { b->value = (b->value << 8) | *b->p++; b->bits += 8; }
  else if (!b->eof) { b->value <<= 8; b->bits += 8; b->eof = 1; }
  else b->bits = 0;
}
static void bd_init(BoolDec *b, const u8 *p, size_t n)
{
  b->p = p; b->end = p + n; b->value = 0; b->range = 254; b->bits = -8; b->eof = 0;
  bd_load(b);
}
static int bd_bit(BoolDec *b, int prob)
{
  u32 range = b->range, split, value;
  int bit;
  if (b->bits < 0) bd_load(b);
  split = (range * (u32)prob) >> 8;
  value = b->value >> b->bits;
  if (value > split) { range -= split; b->value -= (split + 1) << b->bits; bit = 1; }
  else { range = split + 1; bit = 0; }
  while (range < 128) { range <<= 1; b->bits--; }
  b->range = range - 1;
  return bit;
}
static int bd_value(BoolDec *b, int n)
{
  int v = 0;
  while (n-- > 0) v |= bd_bit(b, 128) << n;
  return v;
}
static int bd_signed(BoolDec *b, int n) { int v = bd_value(b, n); return bd_bit(b, 128) ? -v : v; }
static int bd_tree(BoolDec *b, const signed char *tree, const u8 *probs)
{
  int i = 0;
  while ((i = tree[i + bd_bit(b, probs[i >> 1])]) > 0) ;
  return -i;
}

/* ================================================================== VP8: the decoder */
typedef struct {
  int w, h, mbw, mbh;
  BoolDec br, part[8];
  int partmask;
  int use_seg, update_map, abs_delta, seg_q[4], seg_lf[4];
  u8 seg_prob[3];
  int simple, level, sharp, use_lf_delta, ref_lf[4], mode_lf[4], ftype;
  int q[4][6];                     /* per segment: Y1 DC, Y1 AC, Y2 DC, Y2 AC, UV DC, UV AC */
  u8 proba[4 * 8 * 3 * 11];        /* token probabilities [type][band][context][node] */
  int use_skip, skip_p;
  u8 *intra_t, intra_l[4];         /* sub-block modes along the row above and down the left */
  u8 *nz_t, *nzdc_t, nz_l, nzdc_l; /* non-zero contexts, as libwebp packs them */
  u8 *mbf;                         /* per macroblock: segment | i4x4 << 2 | inner edges << 3 */
  u8 *py, *pu, *pv;                /* the frame, whole macroblocks, unfiltered until the end */
  long ys, uvs;
  i16 coef[384];                   /* this macroblock's 16 Y, 4 U and 4 V blocks */
  u8 imodes[16];
} Vp8;

static int clampq(int v, int m) { return v < 0 ? 0 : v > m ? m : v; }

/* the frame header and partition 0's global part (RFC 6386 9.2-9.11, as libwebp reads it) */
static const char *vp8_headers(Vp8 *v, const u8 *d, size_t n)
{
  BoolDec *br = &v->br;
  u32 bits, plen, last, p;
  int i, s, base, dq[5];
  const u8 *sz, *ps;
  size_t left;
  if (n < 10) return "Truncated WebP picture";
  bits = le24(d);
  if (bits & 1) return "The WebP picture is not a key frame";
  if (((bits >> 1) & 7) > 3) return "Bad WebP VP8 frame header";
  if (!((bits >> 4) & 1)) return "The WebP frame is not meant to be shown";
  plen = bits >> 5;
  if (d[3] != 0x9D || d[4] != 0x01 || d[5] != 0x2A) return "Bad WebP VP8 frame signature";
  v->w = (int)(le16(d + 6) & 0x3FFF); v->h = (int)(le16(d + 8) & 0x3FFF);
  if (!v->w || !v->h) return "Bad WebP picture size";
  d += 10; n -= 10;
  if (plen > n) return "Truncated WebP picture";
  bd_init(br, d, plen);
  d += plen; n -= plen;
  bd_bit(br, 128); bd_bit(br, 128);                  /* colour space and clamping type: unused */
  /* segments */
  v->abs_delta = 1;
  v->seg_prob[0] = v->seg_prob[1] = v->seg_prob[2] = 255;
  if ((v->use_seg = bd_bit(br, 128))) {
    v->update_map = bd_bit(br, 128);
    if (bd_bit(br, 128)) {
      v->abs_delta = bd_bit(br, 128);
      for (s = 0; s < 4; s++) v->seg_q[s] = bd_bit(br, 128) ? bd_signed(br, 7) : 0;
      for (s = 0; s < 4; s++) v->seg_lf[s] = bd_bit(br, 128) ? bd_signed(br, 6) : 0;
    }
    if (v->update_map) for (s = 0; s < 3; s++) v->seg_prob[s] = (u8)(bd_bit(br, 128) ? bd_value(br, 8) : 255);
  }
  if (br->eof) return "Damaged WebP segment header";
  /* loop filter */
  v->simple = bd_bit(br, 128);
  v->level = bd_value(br, 6);
  v->sharp = bd_value(br, 3);
  if ((v->use_lf_delta = bd_bit(br, 128)) && bd_bit(br, 128)) {
    for (i = 0; i < 4; i++) if (bd_bit(br, 128)) v->ref_lf[i] = bd_signed(br, 6);
    for (i = 0; i < 4; i++) if (bd_bit(br, 128)) v->mode_lf[i] = bd_signed(br, 6);
  }
  v->ftype = v->level == 0 ? 0 : v->simple ? 1 : 2;
  if (br->eof) return "Damaged WebP filter header";
  /* token partitions: sizes of all but the last, 3 bytes each; a size too big is cut short */
  last = (1u << bd_value(br, 2)) - 1;
  if (n < 3 * last) return "Truncated WebP picture";
  sz = d; ps = d + 3 * last; left = n - 3 * last;
  for (p = 0; p < last; p++, sz += 3) {
    size_t psize = le24(sz);
    if (psize > left) psize = left;
    bd_init(&v->part[p], ps, psize);
    ps += psize; left -= psize;
  }
  bd_init(&v->part[last], ps, left);
  if (!left) return "Truncated WebP picture";
  v->partmask = (int)last;
  /* quantizers */
  base = bd_value(br, 7);
  for (i = 0; i < 5; i++) dq[i] = bd_bit(br, 128) ? bd_signed(br, 4) : 0;
  for (s = 0; s < 4; s++) {
    int q, *m = v->q[s];
    if (v->use_seg) q = v->seg_q[s] + (v->abs_delta ? 0 : base);
    else if (s > 0) { memcpy(m, v->q[0], sizeof v->q[0]); continue; }
    else q = base;
    m[0] = dc_qlookup[clampq(q + dq[0], 127)];
    m[1] = ac_qlookup[clampq(q, 127)];
    m[2] = dc_qlookup[clampq(q + dq[1], 127)] * 2;
    m[3] = (int)(((long)ac_qlookup[clampq(q + dq[2], 127)] * 101581L) >> 16);   /* x * 155 / 100 */
    if (m[3] < 8) m[3] = 8;
    m[4] = dc_qlookup[clampq(q + dq[3], 117)];
    m[5] = ac_qlookup[clampq(q + dq[4], 127)];
  }
  bd_bit(br, 128);                                   /* refresh_entropy_probs: unused */
  for (i = 0; i < 4 * 8 * 3 * 11; i++)
    v->proba[i] = (u8)(bd_bit(br, coeff_update_probs[i]) ? bd_value(br, 8) : default_coeff_probs[i]);
  if ((v->use_skip = bd_bit(br, 128))) v->skip_p = bd_value(br, 8);
  if (br->eof) return "Damaged WebP picture header";
  return NULL;
}

/* one macroblock's modes from partition 0 (RFC 6386 11.2-11.4) */
static void vp8_modes(Vp8 *v, int mbx, int *seg, int *skip, int *i4, int *uvmode)
{
  BoolDec *br = &v->br;
  u8 *top = v->intra_t + 4 * mbx, *left = v->intra_l;
  int x, y;
  *seg = !v->update_map ? 0 : !bd_bit(br, v->seg_prob[0]) ? bd_bit(br, v->seg_prob[1]) : bd_bit(br, v->seg_prob[2]) + 2;
  *skip = v->use_skip ? bd_bit(br, v->skip_p) : 0;
  *i4 = !bd_bit(br, 145);
  if (!*i4) {
    int ym = bd_bit(br, 156) ? (bd_bit(br, 128) ? B_TM : B_HE) : (bd_bit(br, 163) ? B_VE : B_DC);
    v->imodes[0] = (u8)ym;
    memset(top, ym, 4); memset(left, ym, 4);
  } else {
    for (y = 0; y < 4; y++) {
      int ym = left[y];
      for (x = 0; x < 4; x++) {
        ym = bd_tree(br, bmode_tree, kf_bmode_probs + (top[x] * 10 + ym) * 9);
        top[x] = (u8)ym;
        v->imodes[y * 4 + x] = (u8)ym;
      }
      left[y] = (u8)ym;
    }
  }
  *uvmode = !bd_bit(br, 142) ? B_DC : !bd_bit(br, 114) ? B_VE : bd_bit(br, 183) ? B_TM : B_HE;
}

/* one block's tokens (RFC 6386 13), dequantized into out (raster order); returns the position
   after the last token read, as libwebp's GetCoeffs does */
static int vp8_coeffs(BoolDec *br, const u8 *prob, int ctx, int dqdc, int dqac, int n, i16 *out)
{
  const u8 *p = prob + (coeff_bands[n] * 3 + ctx) * 11;
  for (; n < 16; ++n) {
    int v;
    const u8 *pn;
    if (!bd_bit(br, p[0])) return n;                 /* end of block */
    while (!bd_bit(br, p[1])) {                      /* zeros */
      p = prob + coeff_bands[++n] * 33;
      if (n == 16) return 16;
    }
    pn = prob + coeff_bands[n + 1] * 33;
    if (!bd_bit(br, p[2])) { v = 1; p = pn + 11; }
    else {
      if (!bd_bit(br, p[3])) {
        if (!bd_bit(br, p[4])) v = 2;
        else v = 3 + bd_bit(br, p[5]);
      } else if (!bd_bit(br, p[6])) {
        if (!bd_bit(br, p[7])) v = 5 + bd_bit(br, 159);
        else { v = 7 + 2 * bd_bit(br, 165); v += bd_bit(br, 145); }
      } else {
        const u8 *tab;
        int b1 = bd_bit(br, p[8]), b0 = bd_bit(br, p[9 + b1]), cat = 2 * b1 + b0;
        v = 0;
        for (tab = cat3456[cat]; *tab; ++tab) v += v + bd_bit(br, *tab);
        v += 3 + (8 << cat);
      }
      p = pn + 22;
    }
    out[zigzag[n]] = wrap16((bd_bit(br, 128) ? -v : v) * (n > 0 ? dqac : dqdc));
  }
  return 16;
}

/* the inverse Walsh-Hadamard transform of the Y2 block into each luma block's DC (RFC 6386 14.3) */
static void iwht(const i16 *in, i16 *out)
{
  int tmp[16], i;
  for (i = 0; i < 4; i++) {
    int a1 = in[i] + in[12 + i], b1 = in[4 + i] + in[8 + i], c1 = in[4 + i] - in[8 + i], d1 = in[i] - in[12 + i];
    tmp[i] = a1 + b1; tmp[4 + i] = c1 + d1; tmp[8 + i] = a1 - b1; tmp[12 + i] = d1 - c1;
  }
  for (i = 0; i < 4; i++) {
    int a1 = tmp[i * 4] + tmp[i * 4 + 3], b1 = tmp[i * 4 + 1] + tmp[i * 4 + 2];
    int c1 = tmp[i * 4 + 1] - tmp[i * 4 + 2], d1 = tmp[i * 4] - tmp[i * 4 + 3];
    out[(i * 4) * 16] = wrap16((a1 + b1 + 3) >> 3);
    out[(i * 4 + 1) * 16] = wrap16((c1 + d1 + 3) >> 3);
    out[(i * 4 + 2) * 16] = wrap16((a1 - b1 + 3) >> 3);
    out[(i * 4 + 3) * 16] = wrap16((d1 - c1 + 3) >> 3);
  }
}

/* libwebp's code bits for a block: 3 or 2 when it has AC coefficients, else whether DC is set */
static u32 nzbits(u32 nzc, int nz, int dcnz) { return (nzc << 2) | (u32)(nz > 3 ? 3 : nz > 1 ? 2 : dcnz); }

/* the macroblock's coefficients from its token partition (libwebp's ParseResiduals); 1 if they
   are all zero */
static int vp8_residuals(Vp8 *v, BoolDec *tb, int mbx, int i4, int seg)
{
  i16 *dst = v->coef;
  const int *q = v->q[seg];
  const u8 *acp;
  u32 tnz, lnz, nzy = 0, nzuv = 0, out_t, out_l;
  int x, y, ch, first;
  memset(dst, 0, sizeof v->coef);
  if (!i4) {
    i16 dc[16];
    int nz, i;
    memset(dc, 0, sizeof dc);
    nz = vp8_coeffs(tb, v->proba + 1 * 264, v->nzdc_t[mbx] + v->nzdc_l, q[2], q[3], 0, dc);
    v->nzdc_t[mbx] = v->nzdc_l = (u8)(nz > 0);
    if (nz > 1) iwht(dc, dst);
    else { i16 dc0 = wrap16((dc[0] + 3) >> 3); for (i = 0; i < 256; i += 16) dst[i] = dc0; }
    first = 1; acp = v->proba;
  } else { first = 0; acp = v->proba + 3 * 264; }
  tnz = v->nz_t[mbx] & 0x0F; lnz = v->nz_l & 0x0F;
  for (y = 0; y < 4; y++) {
    u32 l = lnz & 1, nzc = 0;
    for (x = 0; x < 4; x++) {
      int nz = vp8_coeffs(tb, acp, (int)(l + (tnz & 1)), q[0], q[1], first, dst);
      l = nz > first;
      tnz = (tnz >> 1) | (l << 7);
      nzc = nzbits(nzc, nz, dst[0] != 0);
      dst += 16;
    }
    tnz >>= 4;
    lnz = (lnz >> 1) | (l << 7);
    nzy = (nzy << 8) | nzc;
  }
  out_t = tnz; out_l = lnz >> 4;
  for (ch = 0; ch < 4; ch += 2) {
    u32 nzc = 0;
    tnz = (u32)v->nz_t[mbx] >> (4 + ch);
    lnz = (u32)v->nz_l >> (4 + ch);
    for (y = 0; y < 2; y++) {
      u32 l = lnz & 1;
      for (x = 0; x < 2; x++) {
        int nz = vp8_coeffs(tb, v->proba + 2 * 264, (int)(l + (tnz & 1)), q[4], q[5], 0, dst);
        l = nz > 0;
        tnz = (tnz >> 1) | (l << 3);
        nzc = nzbits(nzc, nz, dst[0] != 0);
        dst += 16;
      }
      tnz >>= 2;
      lnz = (lnz >> 1) | (l << 5);
    }
    nzuv |= nzc << (4 * ch);
    out_t |= (tnz << 4) << ch;
    out_l |= (lnz & 0xF0) << ch;
  }
  v->nz_t[mbx] = (u8)out_t; v->nz_l = (u8)out_l;
  return !(nzy | nzuv);
}

/* ------------------------------------------------------------------ reconstruction */
static u8 clip255(int v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* the inverse DCT of one block added to the prediction at dst (RFC 6386 14.4). The products are
   formed unsigned so a hostile stream can't overflow a signed multiply; a valid one never wraps */
#define MUL1(a) ((i32)((u32)(a) * 20091u) >> 16) + (a)
#define MUL2(a) ((i32)((u32)(a) * 35468u) >> 16)
static void idct_add(const i16 *in, u8 *dst, long stride)
{
  int tmp[16], i, j;
  for (i = 0; i < 16; i++) if (in[i]) break;
  if (i == 16) return;
  for (i = 1; i < 16; i++) if (in[i]) break;
  if (i == 16) {                                     /* DC only */
    int dc = (in[0] + 4) >> 3;
    for (j = 0; j < 4; j++, dst += stride)
      for (i = 0; i < 4; i++) dst[i] = clip255(dst[i] + dc);
    return;
  }
  for (i = 0; i < 4; i++) {
    int a = in[i] + in[8 + i], b = in[i] - in[8 + i];
    int c = MUL2(in[4 + i]) - (MUL1(in[12 + i]));
    int d = (MUL1(in[4 + i])) + MUL2(in[12 + i]);
    tmp[i] = a + d; tmp[12 + i] = a - d; tmp[4 + i] = b + c; tmp[8 + i] = b - c;
  }
  for (j = 0; j < 4; j++, dst += stride) {
    const int *t = tmp + j * 4;
    int dc = t[0] + 4, a = dc + t[2], b = dc - t[2];
    int c = MUL2(t[1]) - (MUL1(t[3])), d = (MUL1(t[1])) + MUL2(t[3]);
    dst[0] = clip255(dst[0] + ((a + d) >> 3));
    dst[1] = clip255(dst[1] + ((b + c) >> 3));
    dst[2] = clip255(dst[2] + ((b - c) >> 3));
    dst[3] = clip255(dst[3] + ((a - d) >> 3));
  }
}

/* a 16x16 or 8x8 block predicted whole (RFC 6386 12.2): above the frame reads 127, left of it
   129 (and the corner 127 on the top row, 129 below it); DC uses only the edges there are */
static void pred_block(u8 *pl, long st, int x, int y, int n, int mode)
{
  u8 *dst = pl + (long)y * st + x, above[16], left[16];
  int i, j, corner, sh = n == 16 ? 4 : 3;
  for (i = 0; i < n; i++) { above[i] = y ? dst[i - st] : 127; left[i] = x ? dst[(long)i * st - 1] : 129; }
  corner = !y ? 127 : !x ? 129 : dst[-st - 1];
  for (j = 0; j < n; j++, dst += st) {
    if (mode == B_VE) memcpy(dst, above, (size_t)n);
    else if (mode == B_HE) memset(dst, left[j], (size_t)n);
    else if (mode == B_TM) for (i = 0; i < n; i++) dst[i] = clip255(left[j] + above[i] - corner);
    else {
      int s = 0, dcv;
      if (!x && !y) dcv = 128;
      else if (!y) { for (i = 0; i < n; i++) s += left[i]; dcv = (s + (1 << (sh - 1))) >> sh; }
      else if (!x) { for (i = 0; i < n; i++) s += above[i]; dcv = (s + (1 << (sh - 1))) >> sh; }
      else { for (i = 0; i < n; i++) s += above[i] + left[i]; dcv = (s + (1 << sh)) >> (sh + 1); }
      memset(dst, dcv, (size_t)n);
    }
  }
}

static u8 avg2(int a, int b) { return (u8)((a + b + 1) >> 1); }
static u8 avg3(int a, int b, int c) { return (u8)((a + 2 * b + c + 2) >> 2); }

/* a 4x4 sub-block (RFC 6386 12.3), as PhotoCraft's predict_sub: a[0..7] above and above right,
   l[0..3] left, p the corner; out row-major */
static void pred_sub(const u8 *a, const u8 *l, int p, int mode, u8 *out)
{
  u8 e[9], b[16];
  int r, c;
  /* E: the edge from the bottom left, through the corner, to the right */
  e[0] = l[3]; e[1] = l[2]; e[2] = l[1]; e[3] = l[0]; e[4] = (u8)p; e[5] = a[0]; e[6] = a[1]; e[7] = a[2]; e[8] = a[3];
#define E3(i) avg3(e[(i) - 1], e[i], e[(i) + 1])
#define E2(i) avg2(e[i], e[(i) + 1])
#define AP(i) ((i) < 0 ? p : a[i])
#define A3(i) avg3(AP((i) - 1), AP(i), AP((i) + 1))
#define A2(i) avg2(AP(i), AP((i) + 1))
#define LP(i) ((i) < 0 ? p : l[i])
#define L3(i) avg3(LP((i) - 1), LP(i), LP((i) + 1))
#define L2(i) avg2(LP(i), LP((i) + 1))
#define B(row, col) b[(row) * 4 + (col)]
  switch (mode) {
  case B_TM:
    for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) B(r, c) = clip255(l[r] + a[c] - p);
    break;
  case B_VE:
    for (c = 0; c < 4; c++) { u8 v = A3(c); for (r = 0; r < 4; r++) B(r, c) = v; }
    break;
  case B_HE: {
    u8 rows[4];
    rows[0] = L3(0); rows[1] = L3(1); rows[2] = L3(2); rows[3] = avg3(l[2], l[3], l[3]);
    for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) B(r, c) = rows[r];
    break;
  }
  case B_LD:
    B(0, 0) = A3(1);
    B(0, 1) = B(1, 0) = A3(2);
    B(0, 2) = B(1, 1) = B(2, 0) = A3(3);
    B(0, 3) = B(1, 2) = B(2, 1) = B(3, 0) = A3(4);
    B(1, 3) = B(2, 2) = B(3, 1) = A3(5);
    B(2, 3) = B(3, 2) = A3(6);
    B(3, 3) = avg3(a[6], a[7], a[7]);
    break;
  case B_RD:
    B(3, 0) = E3(1);
    B(3, 1) = B(2, 0) = E3(2);
    B(3, 2) = B(2, 1) = B(1, 0) = E3(3);
    B(3, 3) = B(2, 2) = B(1, 1) = B(0, 0) = E3(4);
    B(2, 3) = B(1, 2) = B(0, 1) = E3(5);
    B(1, 3) = B(0, 2) = E3(6);
    B(0, 3) = E3(7);
    break;
  case B_VR:
    B(3, 0) = E3(2);
    B(2, 0) = E3(3);
    B(3, 1) = B(1, 0) = E3(4);
    B(2, 1) = B(0, 0) = E2(4);
    B(3, 2) = B(1, 1) = E3(5);
    B(2, 2) = B(0, 1) = E2(5);
    B(3, 3) = B(1, 2) = E3(6);
    B(2, 3) = B(0, 2) = E2(6);
    B(1, 3) = E3(7);
    B(0, 3) = E2(7);
    break;
  case B_VL:
    B(0, 0) = A2(0);
    B(1, 0) = A3(1);
    B(2, 0) = B(0, 1) = A2(1);
    B(1, 1) = B(3, 0) = A3(2);
    B(2, 1) = B(0, 2) = A2(2);
    B(3, 1) = B(1, 2) = A3(3);
    B(2, 2) = B(0, 3) = A2(3);
    B(3, 2) = B(1, 3) = A3(4);
    B(2, 3) = A3(5);
    B(3, 3) = A3(6);
    break;
  case B_HD:
    B(3, 0) = E2(0);
    B(3, 1) = E3(1);
    B(2, 0) = B(3, 2) = E2(1);
    B(2, 1) = B(3, 3) = E3(2);
    B(2, 2) = B(1, 0) = E2(2);
    B(2, 3) = B(1, 1) = E3(3);
    B(1, 2) = B(0, 0) = E2(3);
    B(1, 3) = B(0, 1) = E3(4);
    B(0, 2) = E3(5);
    B(0, 3) = E3(6);
    break;
  case B_HU:
    B(0, 0) = L2(0);
    B(0, 1) = L3(1);
    B(0, 2) = B(1, 0) = L2(1);
    B(0, 3) = B(1, 1) = L3(2);
    B(1, 2) = B(2, 0) = L2(2);
    B(1, 3) = B(2, 1) = avg3(l[2], l[3], l[3]);
    B(2, 2) = B(2, 3) = l[3];
    for (c = 0; c < 4; c++) B(3, c) = l[3];
    break;
  default: {                                          /* B_DC */
    int s = 4;
    for (c = 0; c < 4; c++) s += a[c] + l[c];
    memset(b, s >> 3, 16);
    break;
  }
  }
#undef E3
#undef E2
#undef AP
#undef A3
#undef A2
#undef LP
#undef L3
#undef L2
#undef B
  memcpy(out, b, 16);
}

/* predict the macroblock and add its residue: luma whole or by sub-block, then chroma */
static void vp8_recon(Vp8 *v, int mbx, int mby, int i4, int uvmode)
{
  long ys = v->ys, uvs = v->uvs;
  int b, k, j;
  if (!i4) {
    u8 *yp = v->py + (long)mby * 16 * ys + mbx * 16;
    pred_block(v->py, ys, mbx * 16, mby * 16, 16, v->imodes[0]);
    for (b = 0; b < 16; b++) idct_add(v->coef + b * 16, yp + (long)(b >> 2) * 4 * ys + (b & 3) * 4, ys);
  } else {
    for (b = 0; b < 16; b++) {
      int bx = mbx * 16 + (b & 3) * 4, by = mby * 16 + (b >> 2) * 4, p;
      u8 a[8], l[4], out[16], *dst = v->py + (long)by * ys + bx;
      for (k = 0; k < 4; k++) { a[k] = by ? dst[k - ys] : 127; l[k] = bx ? dst[(long)k * ys - 1] : 129; }
      p = !by ? 127 : !bx ? 129 : dst[-ys - 1];
      if ((b & 3) == 3) {
        /* the right column's above-right pixels are those above the macroblock to the right
           (the top row's last pixel repeated at the right edge), for all four of its rows */
        const u8 *ar = v->py + ((long)mby * 16 - 1) * ys + mbx * 16;
        for (k = 0; k < 4; k++) a[4 + k] = !mby ? 127 : mbx == v->mbw - 1 ? ar[15] : ar[16 + k];
      } else for (k = 0; k < 4; k++) a[4 + k] = by ? dst[4 + k - ys] : 127;
      pred_sub(a, l, p, v->imodes[b], out);
      for (j = 0; j < 4; j++) memcpy(dst + (long)j * ys, out + j * 4, 4);
      idct_add(v->coef + b * 16, dst, ys);
    }
  }
  for (k = 0; k < 2; k++) {
    u8 *pl = k ? v->pv : v->pu, *cp = pl + (long)mby * 8 * uvs + mbx * 8;
    pred_block(pl, uvs, mbx * 8, mby * 8, 8, uvmode);
    for (b = 0; b < 4; b++) idct_add(v->coef + 256 + k * 64 + b * 16, cp + (long)(b >> 1) * 4 * uvs + (b & 1) * 4, uvs);
  }
}

/* ------------------------------------------------------------------ the loop filter (RFC 6386 15) */
static int sclip1(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }
static int sclip2(int v) { return v < -16 ? -16 : v > 15 ? 15 : v; }
static int iabs(int v) { return v < 0 ? -v : v; }

static void filter2(u8 *p, long s)
{
  int p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s];
  int a = 3 * (q0 - p0) + sclip1(p1 - q1), a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3);
  p[-s] = clip255(p0 + a2); p[0] = clip255(q0 - a1);
}
static void filter4(u8 *p, long s)
{
  int p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s];
  int a = 3 * (q0 - p0), a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3), a3 = (a1 + 1) >> 1;
  p[-2 * s] = clip255(p1 + a3); p[-s] = clip255(p0 + a2); p[0] = clip255(q0 - a1); p[s] = clip255(q1 - a3);
}
static void filter6(u8 *p, long s)
{
  int p2 = p[-3 * s], p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s], q2 = p[2 * s];
  int a = sclip1(3 * (q0 - p0) + sclip1(p1 - q1));
  int a1 = (27 * a + 63) >> 7, a2 = (18 * a + 63) >> 7, a3 = (9 * a + 63) >> 7;
  p[-3 * s] = clip255(p2 + a3); p[-2 * s] = clip255(p1 + a2); p[-s] = clip255(p0 + a1);
  p[0] = clip255(q0 - a1); p[s] = clip255(q1 - a2); p[2 * s] = clip255(q2 - a3);
}
static int hev(const u8 *p, long s, int t) { return iabs(p[-2 * s] - p[-s]) > t || iabs(p[s] - p[0]) > t; }
static int needs(const u8 *p, long s, int t) { return 4 * iabs(p[-s] - p[0]) + iabs(p[-2 * s] - p[s]) <= t; }
static int needs2(const u8 *p, long s, int t, int it)
{
  int p3 = p[-4 * s], p2 = p[-3 * s], p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s], q2 = p[2 * s], q3 = p[3 * s];
  if (4 * iabs(p0 - q0) + iabs(p1 - q1) > t) return 0;
  return iabs(p3 - p2) <= it && iabs(p2 - p1) <= it && iabs(p1 - p0) <= it && iabs(q3 - q2) <= it && iabs(q2 - q1) <= it && iabs(q1 - q0) <= it;
}
/* simple filter across one edge: hs steps across it, vs along it */
static void simple_edge(u8 *p, long hs, long vs, int thresh)
{
  int i, t2 = 2 * thresh + 1;
  for (i = 0; i < 16; i++, p += vs) if (needs(p, hs, t2)) filter2(p, hs);
}
/* normal filter across one edge of n pixels; mb picks the macroblock-edge (6-tap) variant */
static void normal_edge(u8 *p, long hs, long vs, int n, int thresh, int it, int hevt, int mb)
{
  int t2 = 2 * thresh + 1;
  while (n-- > 0) {
    if (needs2(p, hs, t2, it)) {
      if (hev(p, hs, hevt)) filter2(p, hs);
      else if (mb) filter6(p, hs);
      else filter4(p, hs);
    }
    p += vs;
  }
}

/* the whole frame, macroblock by macroblock in raster order as libwebp filters it: left edge,
   inner vertical edges, top edge, inner horizontal edges */
static void vp8_filter(Vp8 *v)
{
  int lim[4][2], ilev[4][2], hevt[4][2], s, i4, mbx, mby, k;
  long ys = v->ys, uvs = v->uvs;
  for (s = 0; s < 4; s++) {
    int base = v->use_seg ? v->seg_lf[s] + (v->abs_delta ? 0 : v->level) : v->level;
    for (i4 = 0; i4 <= 1; i4++) {
      int level = base;
      if (v->use_lf_delta) { level += v->ref_lf[0]; if (i4) level += v->mode_lf[0]; }
      level = level < 0 ? 0 : level > 63 ? 63 : level;
      lim[s][i4] = 0; ilev[s][i4] = 0; hevt[s][i4] = 0;
      if (level > 0) {
        int il = level;
        if (v->sharp > 0) {
          il >>= v->sharp > 4 ? 2 : 1;
          if (il > 9 - v->sharp) il = 9 - v->sharp;
        }
        if (il < 1) il = 1;
        ilev[s][i4] = il;
        lim[s][i4] = 2 * level + il;
        hevt[s][i4] = level >= 40 ? 2 : level >= 15 ? 1 : 0;
      }
    }
  }
  for (mby = 0; mby < v->mbh; mby++)
    for (mbx = 0; mbx < v->mbw; mbx++) {
      int f = v->mbf[(long)mby * v->mbw + mbx], seg = f & 3, i4x = (f >> 2) & 1, inner = (f >> 3) & 1;
      int limit = lim[seg][i4x], il = ilev[seg][i4x], ht = hevt[seg][i4x];
      u8 *yp = v->py + (long)mby * 16 * ys + mbx * 16;
      u8 *up = v->pu + (long)mby * 8 * uvs + mbx * 8, *vp = v->pv + (long)mby * 8 * uvs + mbx * 8;
      if (!limit) continue;
      if (v->ftype == 1) {
        if (mbx > 0) simple_edge(yp, 1, ys, limit + 4);
        if (inner) for (k = 4; k < 16; k += 4) simple_edge(yp + k, 1, ys, limit);
        if (mby > 0) simple_edge(yp, ys, 1, limit + 4);
        if (inner) for (k = 4; k < 16; k += 4) simple_edge(yp + k * ys, ys, 1, limit);
      } else {
        if (mbx > 0) {
          normal_edge(yp, 1, ys, 16, limit + 4, il, ht, 1);
          normal_edge(up, 1, uvs, 8, limit + 4, il, ht, 1);
          normal_edge(vp, 1, uvs, 8, limit + 4, il, ht, 1);
        }
        if (inner) {
          for (k = 4; k < 16; k += 4) normal_edge(yp + k, 1, ys, 16, limit, il, ht, 0);
          normal_edge(up + 4, 1, uvs, 8, limit, il, ht, 0);
          normal_edge(vp + 4, 1, uvs, 8, limit, il, ht, 0);
        }
        if (mby > 0) {
          normal_edge(yp, ys, 1, 16, limit + 4, il, ht, 1);
          normal_edge(up, uvs, 1, 8, limit + 4, il, ht, 1);
          normal_edge(vp, uvs, 1, 8, limit + 4, il, ht, 1);
        }
        if (inner) {
          for (k = 4; k < 16; k += 4) normal_edge(yp + k * ys, ys, 1, 16, limit, il, ht, 0);
          normal_edge(up + 4 * uvs, uvs, 1, 8, limit, il, ht, 0);
          normal_edge(vp + 4 * uvs, uvs, 1, 8, limit, il, ht, 0);
        }
      }
    }
}

/* ------------------------------------------------------------------ YUV to RGB */
/* libwebp's 14-bit fixed point (yuv.h): 1.164 * (Y - 16) and the chroma terms, clipped */
static u8 yuv_clip(int v) { return (u8)((v & ~16383) == 0 ? v >> 6 : v < 0 ? 0 : 255); }
static int mulhi(int v, int c) { return (v * c) >> 8; }
static void yuv_put(u8 *o, int y, int u, int v)
{
  int yy = mulhi(y, 19077);
  o[0] = yuv_clip(yy + mulhi(v, 26149) - 14234);
  o[1] = yuv_clip(yy - mulhi(u, 6419) - mulhi(v, 13320) + 8708);
  o[2] = yuv_clip(yy + mulhi(u, 33050) - 17685);
  o[3] = 255;
}
/* one output row with libwebp's "fancy" upsampling: each chroma sample is weighted 9-3-3-1 from
   the nearest chroma row (nu, nv) and the next nearest (fu, fv), as its UpsampleRgbaLinePair */
static void yuv_row(const u8 *yr, const u8 *nu, const u8 *fu, const u8 *nv, const u8 *fv, u8 *o, int w)
{
  int x, last = (w - 1) >> 1;
  yuv_put(o, yr[0], (3 * nu[0] + fu[0] + 2) >> 2, (3 * nv[0] + fv[0] + 2) >> 2);
  for (x = 1; x <= last; x++) {
    int au = nu[x - 1] + nu[x] + fu[x - 1] + fu[x] + 8, av = nv[x - 1] + nv[x] + fv[x - 1] + fv[x] + 8;
    int dau = (au + 2 * (nu[x] + fu[x - 1])) >> 3, dbu = (au + 2 * (nu[x - 1] + fu[x])) >> 3;
    int dav = (av + 2 * (nv[x] + fv[x - 1])) >> 3, dbv = (av + 2 * (nv[x - 1] + fv[x])) >> 3;
    yuv_put(o + (2 * x - 1) * 4, yr[2 * x - 1], (dau + nu[x - 1]) >> 1, (dav + nv[x - 1]) >> 1);
    yuv_put(o + 2 * x * 4, yr[2 * x], (dbu + nu[x]) >> 1, (dbv + nv[x]) >> 1);
  }
  if (!(w & 1)) yuv_put(o + (w - 1) * 4, yr[w - 1], (3 * nu[last] + fu[last] + 2) >> 2, (3 * nv[last] + fv[last] + 2) >> 2);
}

static void vp8_free(Vp8 *v)
{
  if (!v) return;
  free(v->intra_t); free(v->nz_t); free(v->nzdc_t); free(v->mbf); free(v->py); free(v->pu); free(v->pv);
  free(v);
}

/* a VP8 key frame (the payload of a "VP8 " chunk) into out, opaque */
static const char *vp8_decode(const u8 *d, size_t n, Rgba *out)
{
  Vp8 *v = (Vp8 *)calloc(1, sizeof(Vp8));
  const char *msg;
  int mbx, mby, y;
  if (!v) { codec_oom = 1; return "Out of memory"; }
  if ((msg = vp8_headers(v, d, n))) { vp8_free(v); return msg; }
  if ((long)v->w * v->h > CODEC_MAXPIXELS || v->w > MAXDIM || v->h > MAXDIM) { vp8_free(v); return "The WebP picture is too big"; }
  v->mbw = (v->w + 15) >> 4; v->mbh = (v->h + 15) >> 4;
  v->ys = (long)v->mbw * 16; v->uvs = (long)v->mbw * 8;
  v->intra_t = (u8 *)malloc((size_t)v->mbw * 4);
  v->nz_t = (u8 *)calloc((size_t)v->mbw, 1);
  v->nzdc_t = (u8 *)calloc((size_t)v->mbw, 1);
  v->mbf = (u8 *)malloc((size_t)v->mbw * v->mbh);
  v->py = (u8 *)malloc((size_t)v->ys * v->mbh * 16);
  v->pu = (u8 *)malloc((size_t)v->uvs * v->mbh * 8);
  v->pv = (u8 *)malloc((size_t)v->uvs * v->mbh * 8);
  if (!v->intra_t || !v->nz_t || !v->nzdc_t || !v->mbf || !v->py || !v->pu || !v->pv) { vp8_free(v); codec_oom = 1; return "Out of memory"; }
  memset(v->intra_t, B_DC, (size_t)v->mbw * 4);
  for (mby = 0; mby < v->mbh && !progress(mby, v->mbh); mby++) {
    BoolDec *tb = &v->part[mby & v->partmask];
    memset(v->intra_l, B_DC, 4);
    v->nz_l = v->nzdc_l = 0;
    for (mbx = 0; mbx < v->mbw; mbx++) {
      int seg, skip, i4, uvmode;
      vp8_modes(v, mbx, &seg, &skip, &i4, &uvmode);
      if (v->br.eof) { vp8_free(v); return "Truncated WebP picture (in its first partition)"; }
      if (!skip) skip = vp8_residuals(v, tb, mbx, i4, seg);
      else {
        v->nz_l = v->nz_t[mbx] = 0;
        if (!i4) v->nzdc_l = v->nzdc_t[mbx] = 0;
        memset(v->coef, 0, sizeof v->coef);
      }
      if (tb->eof) { vp8_free(v); return "Truncated WebP picture"; }
      v->mbf[(long)mby * v->mbw + mbx] = (u8)(seg | i4 << 2 | (i4 || !skip) << 3);
      vp8_recon(v, mbx, mby, i4, uvmode);
    }
  }
  if (v->ftype) vp8_filter(v);
  if (!rgba_alloc(out, v->w, v->h)) { vp8_free(v); return "Out of memory"; }
  for (y = 0; y < v->h && !progress(y, v->h); y++) {
    int ch = (v->h + 1) >> 1, nr = y >> 1, fr = (y & 1) ? nr + 1 : nr - 1;
    if (fr < 0) fr = 0;
    if (fr > ch - 1) fr = ch - 1;
    yuv_row(v->py + (long)y * v->ys, v->pu + nr * v->uvs, v->pu + fr * v->uvs, v->pv + nr * v->uvs, v->pv + fr * v->uvs,
            out->px + (size_t)y * v->w * 4, v->w);
  }
  vp8_free(v);
  return NULL;
}

/* ================================================================== VP8L (lossless, RFC 9649) */
/* bits come least significant first; past the end the reader feeds zeros and notes it (eos) */
typedef struct { const u8 *p; size_t n, pos; u32 val; int nbits, phantom, eos; } LBits;

static void lb_fill(LBits *b)
{
  while (b->nbits <= 24) {
    if (b->pos < b->n) b->val |= (u32)b->p[b->pos++] << b->nbits;
    else b->phantom += 8;
    b->nbits += 8;
  }
}
static void lb_used(LBits *b, int k)
{
  b->val >>= k; b->nbits -= k;
  if (b->nbits < b->phantom) { b->eos = 1; b->phantom = b->nbits; }
}
static u32 lb_read(LBits *b, int k)                  /* k <= 24 */
{
  u32 v;
  if (b->nbits < k) lb_fill(b);
  v = b->val & ((1u << k) - 1);
  lb_used(b, k);
  return v;
}

/* a canonical Huffman code: codes of up to 8 bits through a 256-entry table indexed by the next
   8 bits (len << 12 | symbol; 0 for a longer code's prefix), longer ones bit by bit from the
   symbols sorted by length (puff's method). single >= 0 is a code of one symbol and no bits */
typedef struct { u16 *sym; u16 count[16]; u16 root[256]; int single; } Huff;
typedef struct { Huff h[5]; } HGroup;                /* green+length+cache, red, blue, alpha, distance */

/* 1, or 0 for lengths that don't make a complete code (libwebp refuses those), -1 out of memory */
static int huff_build(Huff *h, const u8 *len, int nsym)
{
  u16 offs[16];
  int i, l, nz = 0, last = 0, left = 1, idx;
  u32 code;
  memset(h->count, 0, sizeof h->count);
  memset(h->root, 0, sizeof h->root);
  h->sym = NULL; h->single = -1;
  for (i = 0; i < nsym; i++) if (len[i]) { h->count[len[i]]++; nz++; last = i; }
  if (!nz) return 0;
  if (nz == 1) { h->single = last; return 1; }
  for (l = 1; l < 16; l++) { left = (left << 1) - h->count[l]; if (left < 0) return 0; }
  if (left) return 0;
  if (!(h->sym = (u16 *)malloc(sizeof(u16) * (size_t)nz))) return -1;
  offs[1] = 0;
  for (l = 1; l < 15; l++) offs[l + 1] = (u16)(offs[l] + h->count[l]);
  for (i = 0; i < nsym; i++) if (len[i]) h->sym[offs[len[i]]++] = (u16)i;
  code = 0; idx = 0;
  for (l = 1; l <= 8; l++, code <<= 1)
    for (i = 0; i < h->count[l]; i++, code++) {
      u32 r = 0, c = code;
      int k;
      for (k = 0; k < l; k++) { r = (r << 1) | (c & 1); c >>= 1; }
      for (; r < 256; r += 1u << l) h->root[r] = (u16)((l << 12) | h->sym[idx]);
      idx++;
    }
  return 1;
}
static int huff_read(LBits *b, const Huff *h)
{
  u32 e;
  int code = 0, first = 0, index = 0, l;
  if (h->single >= 0) return h->single;
  if (b->nbits < 15) lb_fill(b);
  if ((e = h->root[b->val & 255])) { lb_used(b, (int)(e >> 12)); return (int)(e & 0xFFF); }
  for (l = 1; l < 16; l++) {
    int count = h->count[l];
    code |= (int)lb_read(b, 1);
    if (code - count < first) return h->sym[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  return 0;                                          /* not reached: the code is complete */
}
static void hgroup_free(HGroup *g) { int i; for (i = 0; i < 5; i++) { free(g->h[i].sym); g->h[i].sym = NULL; } }

typedef struct {
  LBits br;
  u8 lens[256 + 24 + 2048];        /* code lengths being read */
  Huff lh;                         /* the code-length code being used */
  HGroup unused;                   /* a group read only to be checked and dropped */
  const char *msg;
} LDec;                            /* heap-allocated: the Amiga's stack is small */

static int lfail(LDec *d, const char *msg) { if (!d->msg) d->msg = msg; return 0; }

static const u8 cl_order[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* one Huffman code over nsym symbols, simple or with coded code lengths */
static int read_code(LDec *d, Huff *h, int nsym)
{
  LBits *b = &d->br;
  u8 *len = d->lens;
  int r;
  memset(len, 0, (size_t)nsym);
  if (lb_read(b, 1)) {
    int ns = (int)lb_read(b, 1) + 1, s = (int)lb_read(b, lb_read(b, 1) ? 8 : 1);
    if (s < nsym) len[s] = 1;
    if (ns == 2) { s = (int)lb_read(b, 8); if (s < nsym) len[s] = 1; }
  } else {
    u8 cl[19];
    Huff *lh = &d->lh;
    int i, nc = (int)lb_read(b, 4) + 4, maxs, s = 0, prev = 8;
    memset(cl, 0, sizeof cl);
    for (i = 0; i < nc; i++) cl[cl_order[i]] = (u8)lb_read(b, 3);
    if ((r = huff_build(lh, cl, 19)) <= 0) { if (r < 0) codec_oom = 1; return lfail(d, r < 0 ? "Out of memory" : "Damaged WebP lossless code"); }
    if (lb_read(b, 1)) {
      int nb = 2 + 2 * (int)lb_read(b, 3);
      maxs = 2 + (int)lb_read(b, nb);
      if (maxs > nsym) { free(lh->sym); return lfail(d, "Damaged WebP lossless code"); }
    } else maxs = nsym;
    while (s < nsym) {
      int c;
      if (maxs-- == 0) break;
      c = huff_read(b, lh);
      if (c < 16) { len[s++] = (u8)c; if (c) prev = c; }
      else {
        int rep = c == 16 ? 3 + (int)lb_read(b, 2) : c == 17 ? 3 + (int)lb_read(b, 3) : 11 + (int)lb_read(b, 7);
        if (s + rep > nsym) { free(lh->sym); return lfail(d, "Damaged WebP lossless code"); }
        while (rep-- > 0) len[s++] = (u8)(c == 16 ? prev : 0);
      }
      if (b->eos) break;
    }
    free(lh->sym); lh->sym = NULL;
  }
  if (b->eos) return lfail(d, "Truncated WebP lossless data");
  if ((r = huff_build(h, len, nsym)) <= 0) { if (r < 0) codec_oom = 1; return lfail(d, r < 0 ? "Out of memory" : "Damaged WebP lossless code"); }
  return 1;
}

static int read_group(LDec *d, HGroup *g, int cachebits)
{
  int i;
  for (i = 0; i < 5; i++) g->h[i].sym = NULL;
  for (i = 0; i < 5; i++)
    if (!read_code(d, &g->h[i], i == 0 ? 256 + 24 + (cachebits ? 1 << cachebits : 0) : i == 4 ? 40 : 256)) { hgroup_free(g); return 0; }
  return 1;
}

/* per channel modulo 256 */
static u32 addpx(u32 a, u32 b) { return (((a & 0xFF00FF00u) + (b & 0xFF00FF00u)) & 0xFF00FF00u) | (((a & 0x00FF00FFu) + (b & 0x00FF00FFu)) & 0x00FF00FFu); }
static u32 avgpx(u32 a, u32 b) { return (((a ^ b) & 0xFEFEFEFEu) >> 1) + (a & b); }
static int ch8(u32 v, int s) { return (int)((v >> s) & 255); }
static u32 select_px(u32 l, u32 t, u32 tl)
{
  int s, d = 0;
  for (s = 0; s < 32; s += 8) d += iabs(ch8(l, s) - ch8(tl, s)) - iabs(ch8(t, s) - ch8(tl, s));
  return d <= 0 ? t : l;
}
static u32 clamp_full(u32 a, u32 b, u32 c)
{
  u32 r = 0;
  int s;
  for (s = 0; s < 32; s += 8) r |= (u32)clip255(ch8(a, s) + ch8(b, s) - ch8(c, s)) << s;
  return r;
}
static u32 clamp_half(u32 a, u32 b)
{
  u32 r = 0;
  int s;
  for (s = 0; s < 32; s += 8) { int x = ch8(a, s); r |= (u32)clip255(x + (x - ch8(b, s)) / 2) << s; }
  return r;
}
static u32 predict_px(int mode, u32 l, u32 t, u32 tl, u32 tr)
{
  switch (mode) {
  case 1: return l;
  case 2: return t;
  case 3: return tr;
  case 4: return tl;
  case 5: return avgpx(avgpx(l, tr), t);
  case 6: return avgpx(l, tl);
  case 7: return avgpx(l, t);
  case 8: return avgpx(tl, t);
  case 9: return avgpx(t, tr);
  case 10: return avgpx(avgpx(l, tl), avgpx(t, tr));
  case 11: return select_px(l, t, tl);
  case 12: return clamp_full(l, t, tl);
  case 13: return clamp_half(avgpx(l, t), tl);
  }
  return 0xFF000000u;                                /* 0, and 14 and 15 as libwebp treats them */
}
static int s8(u32 v) { return (int)((v & 255) ^ 128) - 128; }

/* (xi, yi) of the 120 short distance codes (RFC 9649 4.2.2) */
static const signed char dist_map[240] = {
  0, 1, 1, 0, 1, 1, -1, 1, 0, 2, 2, 0, 1, 2, -1, 2, 2, 1, -2, 1, 2, 2, -2, 2, 0, 3, 3, 0, 1, 3, -1, 3,
  3, 1, -3, 1, 2, 3, -2, 3, 3, 2, -3, 2, 0, 4, 4, 0, 1, 4, -1, 4, 4, 1, -4, 1, 3, 3, -3, 3, 2, 4, -2, 4,
  4, 2, -4, 2, 0, 5, 3, 4, -3, 4, 4, 3, -4, 3, 5, 0, 1, 5, -1, 5, 5, 1, -5, 1, 2, 5, -2, 5, 5, 2, -5, 2,
  4, 4, -4, 4, 3, 5, -3, 5, 5, 3, -5, 3, 0, 6, 6, 0, 1, 6, -1, 6, 6, 1, -6, 1, 2, 6, -2, 6, 6, 2, -6, 2,
  4, 5, -4, 5, 5, 4, -5, 4, 3, 6, -3, 6, 6, 3, -6, 3, 0, 7, 7, 0, 1, 7, -1, 7, 5, 5, -5, 5, 7, 1, -7, 1,
  4, 6, -4, 6, 6, 4, -6, 4, 2, 7, -2, 7, 7, 2, -7, 2, 3, 7, -3, 7, 7, 3, -7, 3, 5, 6, -5, 6, 6, 5, -6, 5,
  8, 0, 4, 7, -4, 7, 7, 4, -7, 4, 8, 1, 8, 2, 6, 6, -6, 6, 8, 3, 5, 7, -5, 7, 7, 5, -7, 5, 8, 4, 6, 7,
  -6, 7, 7, 6, -7, 6, 8, 5, 7, 7, -7, 7, 8, 6, 8, 7
};
static u32 prefix_value(LBits *b, int sym)
{
  int extra;
  if (sym < 4) return (u32)sym + 1;
  extra = (sym - 2) >> 1;
  return ((u32)(2 + (sym & 1)) << extra) + lb_read(b, extra) + 1;
}

#define DIVB(v, b) (((v) + (1 << (b)) - 1) >> (b))
typedef struct { int type, bits, xs; u32 *data; } LTrans;

/* the inverse transforms, each over the width it was coded at */
static void inv_transform(const LTrans *t, u32 *px, int h)
{
  int x, y, xs = t->xs, bw = DIVB(xs, t->bits);
  if (t->type == 0) {                                /* predictor */
    px[0] = addpx(px[0], 0xFF000000u);
    for (x = 1; x < xs; x++) px[x] = addpx(px[x], px[x - 1]);
    for (y = 1; y < h; y++) {
      u32 *row = px + (long)y * xs, *top = row - xs;
      const u32 *m = t->data + (long)(y >> t->bits) * bw;
      row[0] = addpx(row[0], top[0]);
      for (x = 1; x < xs; x++)                        /* top[xs] is row[0]: the spec's TR at the right edge */
        row[x] = addpx(row[x], predict_px((int)((m[x >> t->bits] >> 8) & 15), row[x - 1], top[x], top[x - 1], top[x + 1]));
    }
  } else if (t->type == 1) {                         /* cross colour */
    for (y = 0; y < h && !progress(y, h); y++) {
      u32 *row = px + (long)y * xs;
      const u32 *m = t->data + (long)(y >> t->bits) * bw;
      for (x = 0; x < xs; x++) {
        u32 c = m[x >> t->bits], a = row[x];
        int g2r = s8(c), g2b = s8(c >> 8), r2b = s8(c >> 16), g = s8(a >> 8);
        int r = (int)((a >> 16) & 255), bl = (int)(a & 255);
        r = (r + ((g2r * g) >> 5)) & 255;
        bl = (bl + ((g2b * g) >> 5)) & 255;
        bl = (bl + ((r2b * s8((u32)r)) >> 5)) & 255;
        row[x] = (a & 0xFF00FF00u) | ((u32)r << 16) | (u32)bl;
      }
    }
  } else if (t->type == 2) {                         /* subtract green */
    long i, n = (long)xs * h;
    for (i = 0; i < n; i++) {
      u32 a = px[i], g = (a >> 8) & 255;
      px[i] = (a & 0xFF00FF00u) | ((((a >> 16) + g) & 255) << 16) | ((a + g) & 255);
    }
  } else {                                           /* colour indexing: expand in place, from the end */
    int pw = DIVB(xs, t->bits), bpp = 8 >> t->bits, ppw = 1 << t->bits;
    u32 mask = (1u << bpp) - 1;
    for (y = h - 1; y >= 0; y--)
      for (x = xs - 1; x >= 0; x--) {
        u32 w = px[(long)y * pw + (x >> t->bits)] >> 8;
        px[(long)y * xs + x] = t->data[(w >> ((x & (ppw - 1)) * bpp)) & mask];
      }
  }
}

/* one image stream of w x h ARGB pixels (RFC 9649 5): a sub-image (!level0: transform data, the
   meta Huffman image, a palette) or, with level0, the picture itself with its transforms read
   and undone. Malloc'd; NULL with d->msg set */
static u32 *vp8l_stream(LDec *d, int w, int h, int level0)
{
  LBits *b = &d->br;
  LTrans tr[4];
  int ntr = 0, seen = 0, xs = w, cbits = 0, mbits = 0, mw = 0, ngroups = 0, nused = 0, i;
  u32 *px = NULL, *cache = NULL, *meta = NULL;
  long *map = NULL;
  HGroup *groups = NULL;
  int ok = 0;
  /* transforms */
  while (level0 && lb_read(b, 1)) {
    LTrans *t = &tr[ntr];
    int type = (int)lb_read(b, 2);
    if (seen & (1 << type)) { lfail(d, "Damaged WebP lossless data (a transform repeats)"); goto done; }
    seen |= 1 << type;
    t->type = type; t->xs = xs; t->bits = 0; t->data = NULL;
    ntr++;
    if (type == 0 || type == 1) {
      t->bits = (int)lb_read(b, 3) + 2;
      if (!(t->data = vp8l_stream(d, DIVB(xs, t->bits), DIVB(h, t->bits), 0))) goto done;
    } else if (type == 3) {
      int nc = (int)lb_read(b, 8) + 1;
      u32 *pal;
      t->bits = nc > 16 ? 0 : nc > 4 ? 1 : nc > 2 ? 2 : 3;
      if (!(pal = vp8l_stream(d, nc, 1, 0))) goto done;
      if (!(t->data = (u32 *)calloc(256, sizeof(u32)))) { free(pal); codec_oom = 1; lfail(d, "Out of memory"); goto done; }
      t->data[0] = pal[0];
      for (i = 1; i < nc; i++) t->data[i] = addpx(pal[i], t->data[i - 1]);
      free(pal);
      xs = DIVB(xs, t->bits);
    }
  }
  /* colour cache */
  if (lb_read(b, 1)) {
    cbits = (int)lb_read(b, 4);
    if (cbits < 1 || cbits > 11) { lfail(d, "Damaged WebP lossless data (colour cache)"); goto done; }
    if (!(cache = (u32 *)calloc((size_t)1 << cbits, sizeof(u32)))) { codec_oom = 1; lfail(d, "Out of memory"); goto done; }
  }
  /* meta Huffman codes: only the groups the meta image uses are kept, but all are read */
  if (level0 && lb_read(b, 1)) {
    long k, nm;
    mbits = (int)lb_read(b, 3) + 2;
    mw = DIVB(xs, mbits);
    if (!(meta = vp8l_stream(d, mw, DIVB(h, mbits), 0))) goto done;
    nm = (long)mw * DIVB(h, mbits);
    for (k = 0; k < nm; k++) { meta[k] = (meta[k] >> 8) & 0xFFFF; if ((int)meta[k] >= ngroups) ngroups = (int)meta[k] + 1; }
    if (!(map = (long *)malloc(sizeof(long) * (size_t)ngroups))) { codec_oom = 1; lfail(d, "Out of memory"); goto done; }
    for (k = 0; k < ngroups; k++) map[k] = -1;
    for (k = 0; k < nm; k++) if (map[meta[k]] < 0) map[meta[k]] = nused++;
    for (k = 0; k < nm; k++) meta[k] = (u32)map[meta[k]];
  } else { ngroups = nused = 1; }
  if (!(groups = (HGroup *)calloc((size_t)nused, sizeof(HGroup)))) { codec_oom = 1; lfail(d, "Out of memory"); goto done; }
  for (i = 0; i < ngroups; i++) {
    HGroup *g = map && map[i] < 0 ? &d->unused : &groups[map ? map[i] : 0];
    if (!read_group(d, g, cbits)) goto fail_groups;
    if (g == &d->unused) hgroup_free(g);
  }
  /* the pixels: literals, back-references and colour cache hits */
  if (!(px = (u32 *)malloc(sizeof(u32) * (size_t)(level0 ? w : xs) * h))) { codec_oom = 1; lfail(d, "Out of memory"); goto fail_groups; }
  {
    long pos = 0, total = (long)xs * h;
    int x = 0, y = 0, mask = mbits ? (1 << mbits) - 1 : -1;
    const HGroup *g = groups;
    while (pos < total) {
      int c;
      if (!(x & mask)) g = &groups[mbits ? meta[(long)(y >> mbits) * mw + (x >> mbits)] : 0];
      c = huff_read(b, &g->h[0]);
      if (c < 256 || c >= 280) {
        u32 p;
        if (c < 256) {
          int r = huff_read(b, &g->h[1]), bl = huff_read(b, &g->h[2]), a = huff_read(b, &g->h[3]);
          p = ((u32)a << 24) | ((u32)r << 16) | ((u32)c << 8) | (u32)bl;
        } else p = cache[c - 280];
        px[pos++] = p;
        if (cache) cache[(0x1E35A7BDu * p) >> (32 - cbits)] = p;
        if (++x >= xs) { x = 0; y++; }
      } else {
        long len = (long)prefix_value(b, c - 256), dist, k;
        int dsym = huff_read(b, &g->h[4]);
        u32 dc = prefix_value(b, dsym);
        if (dc > 120) dist = (long)dc - 120;
        else { dist = dist_map[(dc - 1) * 2] + (long)dist_map[(dc - 1) * 2 + 1] * xs; if (dist < 1) dist = 1; }
        if (b->eos) break;
        if (pos < dist || total - pos < len) { lfail(d, "Damaged WebP lossless data (a copy out of range)"); goto fail_groups; }
        for (k = 0; k < len; k++, pos++) {
          u32 p = px[pos] = px[pos - dist];
          if (cache) cache[(0x1E35A7BDu * p) >> (32 - cbits)] = p;
        }
        x += (int)(len % xs); y += (int)(len / xs);
        if (x >= xs) { x -= xs; y++; }
        if (pos < total && (x & mask)) g = &groups[mbits ? meta[(long)(y >> mbits) * mw + (x >> mbits)] : 0];
      }
      if (b->eos) break;
    }
    if (b->eos) { lfail(d, "Truncated WebP lossless data"); goto fail_groups; }
  }
  for (i = ntr - 1; i >= 0; i--) inv_transform(&tr[i], px, h);
  ok = 1;
fail_groups:
  for (i = 0; i < nused; i++) hgroup_free(&groups[i]);
done:
  for (i = 0; i < ntr; i++) free(tr[i].data);
  free(cache); free(meta); free(map); free(groups);
  if (!ok) { free(px); px = NULL; if (!d->msg) d->msg = "Damaged WebP lossless data"; }
  return px;
}

/* a VP8L picture (the payload of a "VP8L" chunk) into out */
static const char *vp8l_decode(const u8 *d, size_t n, Rgba *out)
{
  LDec *ld;
  u32 *px;
  int w, h;
  long i;
  const char *msg = NULL;
  if (n < 5 || d[0] != 0x2F) return "Bad WebP lossless signature";
  if ((d[4] >> 5) != 0) return "Unknown WebP lossless version";
  w = (int)((le32(d + 1) & 0x3FFF) + 1); h = (int)(((le32(d + 1) >> 14) & 0x3FFF) + 1);
  if ((long)w * h > CODEC_MAXPIXELS) return "The WebP picture is too big";
  if (!(ld = (LDec *)calloc(1, sizeof(LDec)))) { codec_oom = 1; return "Out of memory"; }
  ld->br.p = d + 5; ld->br.n = n - 5;
  if (!(px = vp8l_stream(ld, w, h, 1))) msg = ld->msg;
  else if (!rgba_alloc(out, w, h)) msg = "Out of memory";
  else for (i = 0; i < (long)w * h; i++) {
    u32 p = px[i];
    u8 *o = out->px + i * 4;
    o[0] = (u8)(p >> 16); o[1] = (u8)(p >> 8); o[2] = (u8)p; o[3] = (u8)(p >> 24);
  }
  free(px); free(ld);
  return msg;
}

/* ================================================================== ALPH */
/* an ALPH chunk's alpha plane laid into the picture's alpha bytes: raw or VP8L-compressed (the
   green channel of a headerless VP8L stream), then unfiltered (none, horizontal, vertical,
   gradient, as libwebp's unfilters) */
static const char *alph_decode(const u8 *d, size_t n, Rgba *im)
{
  int w = im->w, h = im->h, method, filter, x, y;
  u8 *a;
  long i, np = (long)w * h;
  if (n < 1) return "Damaged WebP alpha";
  method = d[0] & 3; filter = (d[0] >> 2) & 3;
  if (method > 1 || ((d[0] >> 4) & 3) > 1 || (d[0] >> 6)) return "Damaged WebP alpha";
  if (!(a = (u8 *)malloc((size_t)np))) { codec_oom = 1; return "Out of memory"; }
  if (method == 0) {
    if (n - 1 < (size_t)np) { free(a); return "Truncated WebP alpha"; }
    memcpy(a, d + 1, (size_t)np);
  } else {
    LDec *ld = (LDec *)calloc(1, sizeof(LDec));
    u32 *px;
    if (!ld) { free(a); codec_oom = 1; return "Out of memory"; }
    ld->br.p = d + 1; ld->br.n = n - 1;
    if (!(px = vp8l_stream(ld, w, h, 1))) { const char *m = ld->msg; free(ld); free(a); return m; }
    for (i = 0; i < np; i++) a[i] = (u8)(px[i] >> 8);
    free(px); free(ld);
  }
  if (filter) {
    for (y = 0; y < h; y++) {
      u8 *row = a + (long)y * w, *prev = y ? row - w : NULL;
      if (!prev || filter == 1) {
        int pred = prev ? prev[0] : 0;
        for (x = 0; x < w; x++) { row[x] = (u8)(row[x] + pred); pred = row[x]; }
      } else if (filter == 2) {
        for (x = 0; x < w; x++) row[x] = (u8)(row[x] + prev[x]);
      } else {
        int top, tl = prev[0], left = prev[0];
        for (x = 0; x < w; x++) {
          int g;
          top = prev[x];
          g = left + top - tl;
          g = (g & ~255) == 0 ? g : g < 0 ? 0 : 255;
          left = (u8)(row[x] + g);
          tl = top;
          row[x] = (u8)left;
        }
      }
    }
  }
  for (i = 0; i < np; i++) im->px[i * 4 + 3] = a[i];
  free(a);
  return NULL;
}

/* ================================================================== the RIFF container */
/* one frame from a run of chunks (after VP8X, or an ANMF's payload): an ALPH chunk (the first, or
   with lastalph the last before the picture, as libwebp picks) and a VP8 or VP8L picture */
static const char *frame_decode(const u8 *d, size_t n, int lastalph, Rgba *im)
{
  size_t o = 0;
  const u8 *alph = NULL;
  size_t alen = 0;
  while (o + 8 <= n) {
    size_t len = le32(d + o + 4), avail = n - o - 8;
    const u8 *c = d + o + 8;
    if (len > avail) return "Truncated WebP file";
    if (!memcmp(d + o, "ALPH", 4)) { if (!alph || lastalph) { alph = c; alen = len; } }
    else if (!memcmp(d + o, "VP8 ", 4)) {
      const char *msg = vp8_decode(c, len, im);
      if (!msg && alph && (msg = alph_decode(alph, alen, im))) rgba_free(im);
      return msg;
    } else if (!memcmp(d + o, "VP8L", 4)) return vp8l_decode(c, len, im);
    o += 8 + len + (len & 1);
  }
  return "The WebP file has no picture";
}

int webp_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  size_t end, len;
  const char *msg = NULL;
  out->w = out->h = 0; out->px = NULL;
  if (n < 20 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) { codec_err(err, errcap, "Not a WebP file"); return 0; }
  end = le32(d + 4);
  end = end > n - 8 ? n : end + 8;
  if (end < 20) { codec_err(err, errcap, "Damaged WebP file"); return 0; }
  len = le32(d + 16);
  if (!memcmp(d + 12, "VP8X", 4)) {
    const u8 *c = d + 20;
    i32 cw, ch;
    int flags;
    size_t o;
    if (len < 10 || len > end - 20) { codec_err(err, errcap, "Damaged WebP file"); return 0; }
    flags = c[0];
    cw = (i32)le24(c + 4) + 1; ch = (i32)le24(c + 7) + 1;
    if ((long)cw * ch > CODEC_MAXPIXELS || cw > MAXDIM || ch > MAXDIM) { codec_err(err, errcap, "The WebP picture is too big"); return 0; }
    o = 20 + len + (len & 1);
    if (!(flags & 2)) {
      if (o > end) o = end;
      if (!(msg = frame_decode(d + o, end - o, 1, out)) && (out->w != cw || out->h != ch)) { rgba_free(out); msg = "Damaged WebP file (the picture and canvas sizes differ)"; }
    } else {
      /* an animation: its first frame laid on the transparent canvas */
      msg = "The WebP animation has no frames";
      while (o + 8 <= end) {
        size_t cl = le32(d + o + 4);
        if (cl > end - o - 8) { msg = "Truncated WebP file"; break; }
        if (!memcmp(d + o, "ANMF", 4)) {
          Rgba fr;
          const u8 *f = d + o + 8;
          i32 fx, fy, y;
          if (cl < 16) { msg = "Damaged WebP animation frame"; break; }
          fx = (i32)le24(f) * 2; fy = (i32)le24(f + 3) * 2;
          if ((msg = frame_decode(f + 16, cl - 16, 0, &fr))) break;
          if (fx + fr.w > cw || fy + fr.h > ch) { rgba_free(&fr); msg = "Damaged WebP animation frame (outside the canvas)"; break; }
          if (!rgba_alloc(out, cw, ch)) { rgba_free(&fr); msg = "Out of memory"; break; }
          for (y = 0; y < fr.h; y++) memcpy(out->px + ((size_t)(fy + y) * cw + fx) * 4, fr.px + (size_t)y * fr.w * 4, (size_t)fr.w * 4);
          rgba_free(&fr);
          break;
        }
        o += 8 + cl + (cl & 1);
      }
    }
  } else msg = frame_decode(d + 12, end - 12, 1, out);
  if (msg) { rgba_free(out); codec_err(err, errcap, msg); return 0; }
  return 1;
}

/* ================================================================== 1.2: VP8 (lossy) written */
/* A small key-frame encoder: each macroblock predicted whole (16x16 DC, V, H or TM for luma, the
   same four 8x8 for chroma, the one with the least error chosen), its residual through the forward
   DCT and Walsh-Hadamard transforms, quantized for the quality asked, and coded with VP8's default
   token probabilities (no updates, no segments, no loop filter). The reconstruction the encoder
   predicts from is made with the decoder's own routines above, so the two never drift. Alpha goes
   in an uncompressed ALPH chunk */
typedef struct { Buf o; u32 range, bottom; int count; } BoolEnc;
static void be_init(BoolEnc *e) { buf_init(&e->o); e->range = 255; e->bottom = 0; e->count = 24; }
static void be_carry(BoolEnc *e)
{
  size_t i = e->o.len;
  while (i > 0 && (u8)e->o.data[i - 1] == 255) { e->o.data[i - 1] = 0; i--; }
  if (i > 0) e->o.data[i - 1] = (char)((u8)e->o.data[i - 1] + 1);
}
static void be_put(BoolEnc *e, int prob, int bit)
{
  u32 split = 1 + (((e->range - 1) * (u32)prob) >> 8);
  if (bit) { e->bottom += split; e->range -= split; } else e->range = split;
  while (e->range < 128) {
    e->range <<= 1;
    if (e->bottom & 0x80000000UL) be_carry(e);
    e->bottom <<= 1;
    if (!--e->count) { buf_addc(&e->o, (int)(e->bottom >> 24 & 255)); e->bottom &= 0xFFFFFF; e->count = 8; }
  }
}
static void be_value(BoolEnc *e, u32 v, int n) { while (n-- > 0) be_put(e, 128, (int)(v >> n & 1)); }
static void be_flush(BoolEnc *e)
{
  int c = e->count, k;
  u32 v = e->bottom;
  if (v & (1UL << (32 - c))) be_carry(e);
  v <<= c & 7;
  c >>= 3;
  while (--c >= 0) v <<= 8;
  for (k = 0; k < 4; k++) { buf_addc(&e->o, (int)(v >> 24 & 255)); v <<= 8; }
}
/* the forward transforms (libvpx's) */
static void fdct4(const int *in, int *out)
{
  int tmp[16], i;
  for (i = 0; i < 4; i++) {
    const int *ip = in + 4 * i;
    int a1 = (ip[0] + ip[3]) * 8, b1 = (ip[1] + ip[2]) * 8, c1 = (ip[1] - ip[2]) * 8, d1 = (ip[0] - ip[3]) * 8;
    tmp[4 * i] = a1 + b1; tmp[4 * i + 2] = a1 - b1;
    tmp[4 * i + 1] = (c1 * 2217 + d1 * 5352 + 14500) >> 12;
    tmp[4 * i + 3] = (d1 * 2217 - c1 * 5352 + 7500) >> 12;
  }
  for (i = 0; i < 4; i++) {
    int a1 = tmp[i] + tmp[12 + i], b1 = tmp[4 + i] + tmp[8 + i], c1 = tmp[4 + i] - tmp[8 + i], d1 = tmp[i] - tmp[12 + i];
    out[i] = (a1 + b1 + 7) >> 4; out[8 + i] = (a1 - b1 + 7) >> 4;
    out[4 + i] = ((c1 * 2217 + d1 * 5352 + 12000) >> 16) + (d1 != 0);
    out[12 + i] = (d1 * 2217 - c1 * 5352 + 51000) >> 16;
  }
}
static void fwht4(const int *in, int *out)
{
  int tmp[16], i;
  for (i = 0; i < 4; i++) {
    const int *ip = in + 4 * i;
    int a1 = (ip[0] + ip[2]) * 4, d1 = (ip[1] + ip[3]) * 4, c1 = (ip[1] - ip[3]) * 4, b1 = (ip[0] - ip[2]) * 4;
    tmp[4 * i] = a1 + d1 + (a1 != 0); tmp[4 * i + 1] = b1 + c1; tmp[4 * i + 2] = b1 - c1; tmp[4 * i + 3] = a1 - d1;
  }
  for (i = 0; i < 4; i++) {
    int a1 = tmp[i] + tmp[8 + i], d1 = tmp[4 + i] + tmp[12 + i], c1 = tmp[4 + i] - tmp[12 + i], b1 = tmp[i] - tmp[8 + i];
    int a2 = a1 + d1, b2 = b1 + c1, c2 = b1 - c1, d2 = a1 - d1;
    a2 += a2 < 0; b2 += b2 < 0; c2 += c2 < 0; d2 += d2 < 0;
    out[i] = (a2 + 3) >> 3; out[4 + i] = (b2 + 3) >> 3; out[8 + i] = (c2 + 3) >> 3; out[12 + i] = (d2 + 3) >> 3;
  }
}
/* coefficients (raster order) quantized into zigzag order q; the dequantized ones into dq (raster);
   the position after the last non-zero one from first (0 if none) */
static int quant_block(const int *c, int first, int qdc, int qac, int *q, i16 *dq)
{
  int n, last = 0;
  for (n = 0; n < 16; n++) {
    int r = zigzag[n], d = n ? qac : qdc, v = c[r], a = v < 0 ? -v : v, k;
    if (n < first) { q[n] = 0; continue; }
    k = (a + (n ? d * 3 / 8 : d / 2)) / d;            /* (a little dead zone for AC) */
    if (k > 2048) k = 2048;
    q[n] = v < 0 ? -k : k;
    dq[r] = wrap16(q[n] * d);
    if (k) last = n + 1;
  }
  return last;
}
static void put_coeffs(BoolEnc *e, const u8 *prob, int ctx, int first, const int *q, int last)
{
  const u8 *p = prob + (coeff_bands[first] * 3 + ctx) * 11;
  int n = first;
  while (n < 16) {
    int v, a;
    if (n >= last) { be_put(e, p[0], 0); return; }
    be_put(e, p[0], 1);
    while (!q[n]) { be_put(e, p[1], 0); n++; p = prob + coeff_bands[n] * 33; }
    be_put(e, p[1], 1);
    v = q[n]; a = v < 0 ? -v : v;
    if (a == 1) be_put(e, p[2], 0);
    else {
      be_put(e, p[2], 1);
      if (a <= 4) {
        be_put(e, p[3], 0);
        if (a == 2) be_put(e, p[4], 0); else { be_put(e, p[4], 1); be_put(e, p[5], a == 4); }
      } else {
        be_put(e, p[3], 1);
        if (a <= 10) {
          be_put(e, p[6], 0);
          if (a <= 6) { be_put(e, p[7], 0); be_put(e, 159, a == 6); }
          else { be_put(e, p[7], 1); be_put(e, 165, (a - 7) >> 1); be_put(e, 145, (a - 7) & 1); }
        } else {
          int cat = a < 19 ? 0 : a < 35 ? 1 : a < 67 ? 2 : 3, nb, k;
          const u8 *tab = cat3456[cat];
          u32 extra = (u32)(a - (3 + (8 << cat)));
          be_put(e, p[6], 1);
          be_put(e, p[8], cat >> 1); be_put(e, p[9 + (cat >> 1)], cat & 1);
          for (nb = 0; tab[nb]; nb++) ;
          for (k = 0; k < nb; k++) be_put(e, tab[k], (int)(extra >> (nb - 1 - k) & 1));
        }
      }
    }
    be_put(e, 128, v < 0);
    p = prob + (coeff_bands[n + 1] * 3 + (a == 1 ? 1 : 2)) * 11;
    n++;
  }
}
static const u8 *proba_default(int type) { return default_coeff_probs + type * 264; }
/* the sum of squared differences between source and a block predicted in place */
static long block_sse(const u8 *src, long ss, const u8 *rec, long rs, int n)
{
  long s = 0;
  int x, y;
  for (y = 0; y < n; y++) for (x = 0; x < n; x++) { int d = src[y * ss + x] - rec[y * rs + x]; s += d * d; }
  return s;
}
u8 *webp_encode_lossy(const Rgba *im, int quality, size_t *len)
{
  static const int modes[4] = { B_DC, B_VE, B_HE, B_TM };
  int w = im->w, h = im->h, mbw, mbh, mbx, mby, qi, q[6], alpha = 0, k;
  long ys, uvs, np = (long)w * h, i;
  u8 *sy = NULL, *su = NULL, *sv = NULL, *ry = NULL, *ru = NULL, *rv = NULL, *res = NULL;
  u8 *tnz = NULL, *tdc = NULL;
  u8 lnz = 0, ldc = 0;
  BoolEnc p0, tk;
  Buf f;
  if (!im->px || w < 1 || h < 1 || w > 16383 || h > 16383) return NULL;
  for (i = 0; i < np; i++) if (im->px[i * 4 + 3] != 255) { alpha = 1; break; }
  mbw = (w + 15) / 16; mbh = (h + 15) / 16;
  ys = (long)mbw * 16; uvs = (long)mbw * 8;
  sy = (u8 *)malloc((size_t)ys * mbh * 16); su = (u8 *)malloc((size_t)uvs * mbh * 8); sv = (u8 *)malloc((size_t)uvs * mbh * 8);
  ry = (u8 *)malloc((size_t)ys * mbh * 16); ru = (u8 *)malloc((size_t)uvs * mbh * 8); rv = (u8 *)malloc((size_t)uvs * mbh * 8);
  tnz = (u8 *)calloc((size_t)mbw, 1); tdc = (u8 *)calloc((size_t)mbw, 1);
  if (!sy || !su || !sv || !ry || !ru || !rv || !tnz || !tdc) goto done;
  /* the picture as YUV 4:2:0 (BT.601 studio range, libwebp's sums), over white where transparent
     only in colour (the alpha goes in ALPH), edges repeated to whole macroblocks */
  {
    long x, y;
    for (y = 0; y < mbh * 16; y++)
      for (x = 0; x < ys; x++) {
        const u8 *p = im->px + ((long)(y < h ? y : h - 1) * w + (x < w ? x : w - 1)) * 4;
        sy[y * ys + x] = (u8)((16839 * p[0] + 33059 * p[1] + 6420 * p[2] + 32768 + (16 << 16)) >> 16);
      }
    for (y = 0; y < mbh * 8; y++)
      for (x = 0; x < uvs; x++) {
        int r = 0, g = 0, b = 0, dy, dx;
        for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 2; dx++) {
          long yy = 2 * y + dy, xx = 2 * x + dx;
          const u8 *p = im->px + ((yy < h ? yy : h - 1) * w + (xx < w ? xx : w - 1)) * 4;
          r += p[0]; g += p[1]; b += p[2];
        }
        su[y * uvs + x] = (u8)((-9719 * r - 19081 * g + 28800 * b + (128 << 18) + (1 << 17)) >> 18);
        sv[y * uvs + x] = (u8)((28800 * r - 24116 * g - 4684 * b + (128 << 18) + (1 << 17)) >> 18);
      }
  }
  /* the quantizers, as vp8_headers works them out */
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;
  qi = (100 - quality) * 127 / 100;
  q[0] = dc_qlookup[qi]; q[1] = ac_qlookup[qi]; q[2] = dc_qlookup[qi] * 2;
  q[3] = (int)(((long)ac_qlookup[qi] * 101581L) >> 16); if (q[3] < 8) q[3] = 8;
  q[4] = dc_qlookup[qi > 117 ? 117 : qi]; q[5] = ac_qlookup[qi];
  be_init(&p0); be_init(&tk);
  be_put(&p0, 128, 0); be_put(&p0, 128, 0);              /* colour space, clamping */
  be_put(&p0, 128, 0);                                   /* no segments */
  be_put(&p0, 128, 0); be_value(&p0, 0, 6); be_value(&p0, 0, 3); be_put(&p0, 128, 0);   /* no loop filter */
  be_value(&p0, 0, 2);                                   /* one token partition */
  be_value(&p0, (u32)qi, 7);
  for (k = 0; k < 5; k++) be_put(&p0, 128, 0);
  be_put(&p0, 128, 0);                                   /* refresh_entropy_probs */
  for (k = 0; k < 4 * 8 * 3 * 11; k++) be_put(&p0, coeff_update_probs[k], 0);
  be_put(&p0, 128, 0);                                   /* no skipping */
  for (mby = 0; mby < mbh && !progress(mby, mbh); mby++) {
    lnz = 0; ldc = 0;
    for (mbx = 0; mbx < mbw; mbx++) {
      int best = 0, bu = 0, m, bx, by, c[16], qz[16], nzdc, ch;
      long bestsse = -1;
      i16 coef[384], y2[16];
      int dcs[16];
      u8 *yrec = ry + (long)mby * 16 * ys + mbx * 16;
      const u8 *ysrc = sy + (long)mby * 16 * ys + mbx * 16;
      u32 t = tnz[mbx] & 0x0F, l = lnz & 0x0F, outt = 0, outl = 0;
      /* the luma mode with the least error */
      for (m = 0; m < 4; m++) {
        long e;
        pred_block(ry, ys, mbx * 16, mby * 16, 16, modes[m]);
        e = block_sse(ysrc, ys, yrec, ys, 16);
        if (bestsse < 0 || e < bestsse) { bestsse = e; best = m; }
      }
      pred_block(ry, ys, mbx * 16, mby * 16, 16, modes[best]);
      bestsse = -1;
      for (m = 0; m < 4; m++) {
        long e;
        pred_block(ru, uvs, mbx * 8, mby * 8, 8, modes[m]);
        pred_block(rv, uvs, mbx * 8, mby * 8, 8, modes[m]);
        e = block_sse(su + (long)mby * 8 * uvs + mbx * 8, uvs, ru + (long)mby * 8 * uvs + mbx * 8, uvs, 8) +
            block_sse(sv + (long)mby * 8 * uvs + mbx * 8, uvs, rv + (long)mby * 8 * uvs + mbx * 8, uvs, 8);
        if (bestsse < 0 || e < bestsse) { bestsse = e; bu = m; }
      }
      pred_block(ru, uvs, mbx * 8, mby * 8, 8, modes[bu]);
      pred_block(rv, uvs, mbx * 8, mby * 8, 8, modes[bu]);
      /* its modes in partition 0 */
      be_put(&p0, 145, 1);
      if (modes[best] == B_DC) { be_put(&p0, 156, 0); be_put(&p0, 163, 0); }
      else if (modes[best] == B_VE) { be_put(&p0, 156, 0); be_put(&p0, 163, 1); }
      else if (modes[best] == B_HE) { be_put(&p0, 156, 1); be_put(&p0, 128, 0); }
      else { be_put(&p0, 156, 1); be_put(&p0, 128, 1); }
      if (modes[bu] == B_DC) be_put(&p0, 142, 0);
      else if (modes[bu] == B_VE) { be_put(&p0, 142, 1); be_put(&p0, 114, 0); }
      else if (modes[bu] == B_TM) { be_put(&p0, 142, 1); be_put(&p0, 114, 1); be_put(&p0, 183, 1); }
      else { be_put(&p0, 142, 1); be_put(&p0, 114, 1); be_put(&p0, 183, 0); }
      /* luma: each block's transform, the DCs through Y2 */
      memset(coef, 0, sizeof coef);
      {
        int ac[16][16];
        for (by = 0; by < 4; by++) for (bx = 0; bx < 4; bx++) {
          int px[16], x, y;
          for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) px[y * 4 + x] = ysrc[(by * 4 + y) * ys + bx * 4 + x] - yrec[(by * 4 + y) * ys + bx * 4 + x];
          fdct4(px, ac[by * 4 + bx]);
          dcs[by * 4 + bx] = ac[by * 4 + bx][0];
        }
        fwht4(dcs, c);
        memset(y2, 0, sizeof y2);
        nzdc = quant_block(c, 0, q[2], q[3], qz, y2);
        put_coeffs(&tk, proba_default(1), tdc[mbx] + ldc, 0, qz, nzdc);
        tdc[mbx] = ldc = (u8)(nzdc > 0);
        if (nzdc > 1) iwht(y2, coef);
        else { i16 dc0 = wrap16((y2[0] + 3) >> 3); for (k = 0; k < 256; k += 16) coef[k] = dc0; }
        for (by = 0; by < 4; by++) {
          u32 lb = l & 1;
          for (bx = 0; bx < 4; bx++) {
            int n = by * 4 + bx, last;
            i16 *dq = coef + n * 16;
            last = quant_block(ac[n], 1, q[0], q[1], qz, dq);
            put_coeffs(&tk, proba_default(0), (int)(lb + (t & 1)), 1, qz, last);
            lb = last > 1;
            t = (t >> 1) | (lb << 7);
          }
          t >>= 4;
          l = (l >> 1) | (lb << 7);
        }
        outt = t; outl = l >> 4;
      }
      /* chroma */
      for (ch = 0; ch < 2; ch++) {
        u8 *src = (ch ? sv : su) + (long)mby * 8 * uvs + mbx * 8, *rec = (ch ? rv : ru) + (long)mby * 8 * uvs + mbx * 8;
        u32 tc = (u32)tnz[mbx] >> (4 + 2 * ch), lc = (u32)lnz >> (4 + 2 * ch);
        for (by = 0; by < 2; by++) {
          u32 lb = lc & 1;
          for (bx = 0; bx < 2; bx++) {
            int px[16], x, y, last;
            i16 *dq = coef + 256 + ch * 64 + (by * 2 + bx) * 16;
            for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) px[y * 4 + x] = src[(by * 4 + y) * uvs + bx * 4 + x] - rec[(by * 4 + y) * uvs + bx * 4 + x];
            fdct4(px, c);
            last = quant_block(c, 0, q[4], q[5], qz, dq);
            put_coeffs(&tk, proba_default(2), (int)(lb + (tc & 1)), 0, qz, last);
            lb = last > 0;
            tc = (tc >> 1) | (lb << 3);
          }
          tc >>= 2;
          lc = (lc >> 1) | (lb << 5);
        }
        outt |= (tc << 4) << (2 * ch);
        outl |= (lc & 0xF0) << (2 * ch);
      }
      tnz[mbx] = (u8)outt; lnz = (u8)outl;
      /* the reconstruction, as the decoder makes it */
      for (by = 0; by < 4; by++) for (bx = 0; bx < 4; bx++) idct_add(coef + (by * 4 + bx) * 16, yrec + (long)by * 4 * ys + bx * 4, ys);
      for (ch = 0; ch < 2; ch++) for (by = 0; by < 2; by++) for (bx = 0; bx < 2; bx++)
        idct_add(coef + 256 + ch * 64 + (by * 2 + bx) * 16, (ch ? rv : ru) + ((long)mby * 8 + by * 4) * uvs + mbx * 8 + bx * 4, uvs);
    }
  }
  be_flush(&p0); be_flush(&tk);
  if (p0.o.oom || tk.o.oom) { buf_free(&p0.o); buf_free(&tk.o); goto done; }
  /* the container */
  {
    size_t vlen = 10 + p0.o.len + tk.o.len, alen = alpha ? 1 + (size_t)np : 0;
    u32 tag = (u32)(0 | (0 << 1) | (1 << 4) | ((u32)p0.o.len << 5));
    buf_init(&f);
    buf_add(&f, "RIFF\0\0\0\0WEBP", 12);
    if (alpha) {
      u8 x[18];
      memcpy(x, "VP8X", 4); x[4] = 10; x[5] = x[6] = x[7] = 0;
      x[8] = 0x10; x[9] = x[10] = x[11] = 0;
      x[12] = (u8)(w - 1); x[13] = (u8)((w - 1) >> 8); x[14] = (u8)((w - 1) >> 16);
      x[15] = (u8)(h - 1); x[16] = (u8)((h - 1) >> 8); x[17] = (u8)((h - 1) >> 16);
      buf_add(&f, x, 18);
      buf_add(&f, "ALPH", 4); buf_addc(&f, (int)(alen & 255)); buf_addc(&f, (int)(alen >> 8 & 255)); buf_addc(&f, (int)(alen >> 16 & 255)); buf_addc(&f, (int)(alen >> 24 & 255));
      buf_addc(&f, 0);                                   /* raw, no filter, no preprocessing */
      for (i = 0; i < np; i++) buf_addc(&f, im->px[i * 4 + 3]);
      if (alen & 1) buf_addc(&f, 0);
    }
    buf_add(&f, "VP8 ", 4); buf_addc(&f, (int)(vlen & 255)); buf_addc(&f, (int)(vlen >> 8 & 255)); buf_addc(&f, (int)(vlen >> 16 & 255)); buf_addc(&f, (int)(vlen >> 24 & 255));
    buf_addc(&f, (int)(tag & 255)); buf_addc(&f, (int)(tag >> 8 & 255)); buf_addc(&f, (int)(tag >> 16 & 255));
    buf_addc(&f, 0x9D); buf_addc(&f, 0x01); buf_addc(&f, 0x2A);
    buf_addc(&f, w & 255); buf_addc(&f, (w >> 8) & 0x3F); buf_addc(&f, h & 255); buf_addc(&f, (h >> 8) & 0x3F);
    buf_add(&f, p0.o.data, p0.o.len); buf_add(&f, tk.o.data, tk.o.len);
    if (vlen & 1) buf_addc(&f, 0);
    buf_free(&p0.o); buf_free(&tk.o);
    if (f.oom) { buf_free(&f); goto done; }
    { u32 rs = (u32)(f.len - 8); f.data[4] = (char)(rs & 255); f.data[5] = (char)(rs >> 8 & 255); f.data[6] = (char)(rs >> 16 & 255); f.data[7] = (char)(rs >> 24 & 255); }
    *len = f.len;
    res = (u8 *)buf_take(&f);
  }
done:
  free(sy); free(su); free(sv); free(ry); free(ru); free(rv); free(tnz); free(tdc);
  return res;
}
