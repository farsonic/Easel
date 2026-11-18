/*
 * colorids.h - the menu IDs of Easel 1.2's colour and view additions (colorui.c): View > Pixel
 * Aspect Ratio and its correction, Proof Colors, Gamut Warning and Proof Setup; Edit > Color
 * Settings, Assign Profile and Convert to Profile; Image > Mode > CMYK Color and Lab Color; Layer >
 * Type's OpenType features. Kept
 * apart from ids.h, in a range of their own (5000-5099).
 */
#ifndef EASEL_COLORIDS_H
#define EASEL_COLORIDS_H
#define ID_CU_FIRST     5000
#define ID_PROOF        5032           /* View > Proof Colors */
#define ID_GAMUT        5033           /* View > Gamut Warning */
#define ID_PROOFSET0    5040           /* + PRF_CMYK .. PRF_DEUTAN (cms.h) */
#define ID_LOADCMYK     5060           /* View > Proof Setup > Load Working CMYK Profile... */
#define ID_PRESSCMYK    5061           /* View > Proof Setup > Easel's Press (built-in CMYK) */
#define ID_COLORSET     5062           /* Edit > Color Settings... */
#define ID_ASSIGNPROF   5063
#define ID_CONVERTPROF  5064
#define ID_MODE_CMYK    5065
#define ID_MODE_LAB     5066
#define ID_OT0          5070           /* + 0..TO_NFEAT-1: Layer > Type's OpenType features (type.h's TO_LIGA << k) */
#define ID_TOSHAPE      5080           /* Layer > Type > Convert to Shape */
#define ID_UPDATETEXT   5081           /* Layer > Type > Update All Text Layers */
#define ID_CU_LAST      5099
#endif
