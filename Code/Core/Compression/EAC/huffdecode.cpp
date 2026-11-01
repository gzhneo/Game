/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Copyright (C) Electronic Arts Canada Inc. 1995-2002.  All rights reserved.

#ifndef __HUFREAD
#define __HUFREAD 1

#include <string.h>
#include "codex.h"
#include "huffcodex.h"

#if defined(_MSC_VER)
#pragma warning(push,1)
#endif

/****************************************************************/
/*  Internal Functions                                          */
/****************************************************************/

struct HuffDecodeContext
{
    unsigned char  *s;
    int             bitsleft;
    unsigned int   bits;
};

/****************************************************************/
/*  Huffman Unpacker                                            */
/****************************************************************/

static int ZERO=0;

#define GET16BITS() \
    bitsunshifted =  qs[0] | (bitsunshifted << 8);\
    bitsunshifted =  qs[1] | (bitsunshifted << 8);\
    qs += 2;


#define SQgetbits(v,n)\
    if (n) \
    { \
        v = bits >> (32-(n));\
        bits <<= (n);\
        bitsleft -= (n);\
    } \
\
    if (bitsleft<0)\
    {\
        GET16BITS() \
\
        bits = bitsunshifted<<(-bitsleft);\
        bitsleft += 16;\
    }


#define SQgetnum(v) \
    if ((int)bits<0)\
    {\
        SQgetbits(v,3);\
        v -= 4;\
    }\
    else\
    {\
        int             n;\
        unsigned int   v1;\
\
        if (bits>>16)\
        {\
            n=2;\
            do\
            {\
                bits <<= 1; \
                ++n;\
            }\
            while ((int)bits>=0);\
            bits <<= 1;\
            bitsleft -= (n-1);\
            SQgetbits(v,ZERO);\
        }\
        else\
        {\
            n=2;\
            do\
            {\
                ++n;\
                SQgetbits(v,1);\
            }\
            while (!v);\
        }\
        if (n>16)\
        {\
            SQgetbits(v,n-16);\
            SQgetbits(v1,16);\
            v = (v1|(v<<16))+(1<<n)-4;\
        }\
        else\
        {\
            SQgetbits(v,n);\
            v = (v+(1<<n)-4);\
        }\
    }

/* note:  requires 'amt' be a multiple of 16 */
#define SQmemset(ptr, val, amt) \
{\
    int i = amt/16;\
    int intval = (val<<24)|(val<<16)|(val<<8)|val;\
    int* lptr = (int*)ptr;\
\
    while (i)\
    {\
        lptr[0] = intval;\
        lptr[1] = intval;\
        lptr[2] = intval;\
        lptr[3] = intval;\
        lptr += 4;\
        --i;\
    }\
}\

static int HUFF_decompress(unsigned char *packbuf, unsigned char *unpackbuf)
{
    unsigned int    type;
    unsigned char   clue;
    int            ulen;
    unsigned int    cmp;
    int             bitnum=0;
    int             cluelen=0;
    unsigned char   *qs;
    unsigned char   *qd;
    unsigned int   bits;
    unsigned int   bitsunshifted=0;
    int             numbits;
    int             bitsleft;
    unsigned int   v;

    qs = packbuf;
    qd = unpackbuf;
    ulen = 0L;

    if (qs)
    {
        {
            int             mostbits;
            int             i;
            int             bitnumtbl[16];
            unsigned int    deltatbl[16];
            unsigned int    cmptbl[16];
            unsigned char   codetbl[256];
            unsigned char   quickcodetbl[256];
            unsigned char   quicklentbl[256];

            bitsleft = -16;                                 /* init bit stream */
            bits = 0;
            SQgetbits(v,ZERO);

            SQgetbits(type,16);

            if (type&0x8000) /* 4 byte size field */
            {
                /* (skip nothing for 0x30fb) */
                if (type&0x100)                                 /* skip ulen */
                {
                    SQgetbits(v,16);
                    SQgetbits(v,16);
                }
                type &= ~0x100;

                SQgetbits(v,16);                                 /* unpack len */
                SQgetbits(ulen,16);
                ulen |= (v<<16);
            }
            else
            {
                /* (skip nothing for 0x30fb) */
                if (type&0x100)                                 /* skip ulen */
                {
                    SQgetbits(v,8);
                    SQgetbits(v,16);
                }
                type &= ~0x100;

                SQgetbits(v,8);                                 /* unpack len */
                SQgetbits(ulen,16);
                ulen |= (v<<16);
            }

            {
                {
                    int numchars;

                    {
                        unsigned int basecmp;

                        {
                            unsigned int t;
                            SQgetbits(t,8);                          /* clue byte */
                            clue = static_cast<unsigned char>(t);
                        }

                        numchars = 0;
                        numbits = 1;
                        basecmp = static_cast<unsigned int>(0);

                        /* decode bitnums */

                        do
                        {
                            basecmp <<= 1;
                            deltatbl[numbits] = basecmp-numchars;

                            SQgetnum(bitnum);               /* # of codes of n bits */
                            bitnumtbl[numbits] = bitnum;

                            numchars += bitnum;
                            basecmp += bitnum;

                            cmp = 0;
                            if (bitnum)                             /* left justify cmp */
                                cmp = (basecmp << (16-numbits) & 0xffff);

                            cmptbl[numbits++] = cmp;

                        }
                        while (!bitnum || cmp);                     /* n+1 bits in cmp? */
                    }
                    cmptbl[numbits-1] = 0xffffffff;               /* force match on most bits */

                    mostbits = numbits-1;

                    /* decode leapfrog code table */

                    {
                        signed char     leap[256];
                        unsigned char   nextchar;

                        SQmemset(leap,0,256);
                        nextchar = static_cast<unsigned char>(-1);

                        for (i=0;i<numchars;++i)
                        {
                            int leapdelta=0;

                            SQgetnum(leapdelta);
                            ++leapdelta;

                            do
                            {
                                ++nextchar;
                                if (!leap[nextchar])
                                    --leapdelta;
                            } while (leapdelta);

                            leap[nextchar] = 1;
                            codetbl[i] = nextchar;
                        }
                    }
                }

/****************************************************************/
/*  Make fast 8 tables                                          */
/****************************************************************/

                SQmemset(quicklentbl,64,256);

                {
                    int bits;
                    int bitnum;
                    int numbitentries;
                    int nextcode;
                    int nextlen;
                    int i;
                    unsigned char *codeptr;
                    unsigned char *quickcodeptr;
                    unsigned char *quicklenptr;

                    codeptr = codetbl;
                    quickcodeptr = quickcodetbl;
                    quicklenptr = quicklentbl;

                    for (bits=1; bits<=mostbits; ++bits)
                    {
                        bitnum = bitnumtbl[bits];
                        if (bits>=9)
                            break;
                        numbitentries = 1<<(8-bits);

                        while (bitnum--)
                        {
                            nextcode = *codeptr++;
                            nextlen = bits;
                            if (nextcode==clue)
                            {
                                cluelen = bits;
                                nextlen = 96;                   /* will force out of main loop */
                            }
                            for (i=0; i<numbitentries; ++i)
                            {
                                *quickcodeptr++ = static_cast<unsigned char>(nextcode);
                                *quicklenptr++ = static_cast<unsigned char>(nextlen);
                            }
                        }
                    }
                }
            }

/****************************************************************/
/*  Main decoder                                                */
/****************************************************************/

            for (;;)
            {
                unsigned char   *quickcodeptr = quickcodetbl;
                unsigned char   *quicklenptr  = quicklentbl;

/* quick 8 decode, then quick 8 fetch while fetching 16 bits would do it */

                for (;;)
                {
                    numbits = quicklenptr[bits>>24];
                    bitsleft -= numbits;

                    if (bitsleft>=0)
                    {
                        do
                        {
                            *qd++ = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            *qd++ = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            *qd++ = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            *qd++ = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;

                        } while (bitsleft>=0);
                    }
                    bitsleft += 16;
                    if (bitsleft<0)
                        break;

/* quick 8 fetch */

                    *qd++ = quickcodeptr[bits>>24];
                    GET16BITS();
                    bits = bitsunshifted<<(16-bitsleft);
                }

                bitsleft = bitsleft-16+numbits;   /* back to normal */

/****************************************************************/
/*  16 bit decoder                                              */
/****************************************************************/

                {
                    unsigned char   code;


                    if (numbits!=96)
                    {
                        cmp = static_cast<unsigned int>(bits>>16);  /* 16 bit left justified compare */

                        numbits = 8;
                        do
                        {
                            ++numbits;
                        }
                        while (cmp>=cmptbl[numbits]);
                    }
                    else
                        numbits = cluelen;


                    cmp = bits >> (32-(numbits));
                    bits <<= (numbits);
                    bitsleft -= (numbits);

                    code = codetbl[cmp-deltatbl[numbits]];  /* the code */

                    if (code!=clue && bitsleft>=0)
                    {
                        *qd++ = code;
                        continue;
                    }

                    if (bitsleft<0)
                    {
                        GET16BITS();
                        bits = bitsunshifted<<-bitsleft;
                        bitsleft += 16;
                    }

                    if (code!=clue)
                    {
                        *qd++ = code;
                        continue;
                    }

                    /* handle clue */

                    {
                        int    runlen=0;
                        unsigned char *d=qd;
                        unsigned char *dest;

                        SQgetnum(runlen);
                        if (runlen)                             /* runlength sequence */
                        {
                            dest = d+runlen;
                            code = *(d-1);
                            do
                            {
                                *d++ = code;
                            } while (d<dest);

                            qd = d;
                            continue;
                        }
                    }

                    SQgetbits(v,1);                         /* End Of File */
                    if (v)
                        break;

                    {
                        unsigned int t;
                        SQgetbits(t,8);                    /* explicite byte */
                        code = static_cast<unsigned char>(t);
                    }
                    *qd++ = code;
                    continue;
                }

            }


/****************************************************************/
/*  Undelta                                                     */
/****************************************************************/

            {
                int i;
                int nextchar;

                if (type==0x32fb || type==0xb2fb)                           /* deltaed? */
                {
                    i = 0;
                    qd = unpackbuf;
                    while (qd<unpackbuf+ulen)
                    {
                        i += static_cast<int>(*qd);
                        *qd++ = static_cast<unsigned char>(i);
                    }
                }
                else if (type==0x34fb || type==0xb4fb)                      /* accelerated? */
                {

                    i = 0;
                    nextchar = 0;
                    qd = unpackbuf;
                    while (qd<unpackbuf+ulen)
                    {
                        i += static_cast<int>(*qd);
                        nextchar += i;
                        *qd++ = static_cast<unsigned char>(nextchar);
                    }
                }
            }
        }
    }
    return(ulen);
}

/****************************************************************/
/*  Bounded decoding                                            */
/****************************************************************/
/*
** Like REF_decode and BTREE_decode, the Huffman decoder above follows its input unchecked. The
** bit stream macros read past the end of the source, the stack tables (cmptbl[16], codetbl[256],
** quickcodetbl[256]) are indexed with values from the file, and the run length sequence writes
** as many bytes as the file says.
**
** This version produces the same output but checks each of these:
**   * source: GET16BITS never reads past `qsend`; at the end of the stream it returns one bits
**     and sets `overflow` (zeros could make the bit macros loop forever, see SQgetnum).
**   * destination: every write goes through PUTBYTE and lands in a trash variable past the bound.
**   * tables: numbits, numchars and the quick code entries are checked against their array sizes.
**   * run lengths: the run must fit into the destination and needs a preceding byte.
** After a violation the function leaves its loops and returns 0.
**
** For valid data the result is byte for byte the same as HUFF_decode.
*/

/* Bounded write: past qdend the byte lands in huffTrash. */
#define PUTBYTE(p)  (*(((p) < qdend) ? (p)++ : (overflow = 1, &huffTrash)))

/* Bounded read:
**
**   1. The bit reader always fetches two bytes ahead, so even valid data reads past the last
**      needed byte. A few such reads must be allowed or valid streams would be rejected; they
**      supply zero bits.
**   2. After that, further reads supply one bits and set `overflow`. Ones, because with zeros
**      the bit macros can loop forever (`SQgetnum`: `while ((int)bits>=0)`); ones end them. */
/* Bounded number read. The first loop of SQgetnum consumes no input:
**
**     do { bits <<= 1; ++n; } while ((int)bits>=0);
**
** If `bits` is 0 it never ends, so a corrupt file would hang the game. Here n is limited to 32;
** beyond that the stream is invalid. The second loop consumes bits and ends through the padding
** with one bits. */
/* After the limit n can reach 33; the shifts below are done in 64 bits so that case stays defined
** (the result is thrown away through `overflow`). For n <= 31 the values are unchanged. */
#define SQGETNUM_MAX_N 32
#undef SQgetnum
#define SQgetnum(v) \
    if ((int)bits<0)\
    {\
        SQgetbits(v,3);\
        v -= 4;\
    }\
    else\
    {\
        int             n;\
        unsigned int   v1;\
\
        if (bits>>16)\
        {\
            n=2;\
            do\
            {\
                bits <<= 1; \
                ++n;\
                if (n > SQGETNUM_MAX_N) { overflow = 1; break; }\
            }\
            while ((int)bits>=0);\
            bits <<= 1;\
            bitsleft -= (n-1);\
            SQgetbits(v,ZERO);\
        }\
        else\
        {\
            n=2;\
            do\
            {\
                ++n;\
                if (n > SQGETNUM_MAX_N) { overflow = 1; break; }\
                SQgetbits(v,1);\
            }\
            while (!v);\
        }\
        if (n>16)\
        {\
            SQgetbits(v,n-16);\
            SQgetbits(v1,16);\
            v = (v1|(static_cast<unsigned int>(v)<<16))+static_cast<unsigned int>((1ull<<n)-4);\
        }\
        else\
        {\
            SQgetbits(v,n);\
            v = (v+static_cast<unsigned int>((1ull<<n)-4));\
        }\
    }

#define HUFF_MAX_PAD_BYTES 8
#undef GET16BITS
#define GET16BITS() \
    { \
        if (qs + 2 <= qsend) \
        { \
            bitsunshifted =  qs[0] | (bitsunshifted << 8); \
            bitsunshifted =  qs[1] | (bitsunshifted << 8); \
            qs += 2; \
        } \
        else \
        { \
            /* Pad per byte: existing bytes are read, only missing ones are replaced. */ \
            unsigned int huffB0 = (qs + 0 < qsend) ? (unsigned int)qs[0] : 0u; \
            unsigned int huffB1 = (qs + 1 < qsend) ? (unsigned int)qs[1] : 0u; \
            padBytes += (int)((qs + 0 < qsend) ? 0 : 1) + (int)((qs + 1 < qsend) ? 0 : 1); \
            if (padBytes > HUFF_MAX_PAD_BYTES) \
            { \
                overflow = 1; \
                huffB0 = 0xFFu; \
                huffB1 = 0xFFu; \
            } \
            bitsunshifted = huffB0 | (bitsunshifted << 8); \
            bitsunshifted = huffB1 | (bitsunshifted << 8); \
            qs += 2; \
        } \
    }

static int HUFF_decompress_bounded(unsigned char *packbuf, int packsize,
                                   unsigned char *unpackbuf, int unpacksize)
{
    /* Bounds: see the comment above HUFF_decode_bounded. */
    unsigned char  *qsend;
    unsigned char  *qdstart;
    unsigned char  *qdend;
    unsigned char   huffTrash = 0;   /* sink for writes past the bound */
    int             overflow = 0;
    int             padBytes = 0;       /* padded bytes past the source end (see GET16BITS) */
    unsigned int    type;
    unsigned char   clue;
    int            ulen;
    unsigned int    cmp;
    int             bitnum=0;
    int             cluelen=0;
    unsigned char   *qs;
    unsigned char   *qd;
    unsigned int   bits;
    unsigned int   bitsunshifted=0;
    int             numbits;
    int             bitsleft;
    unsigned int   v;

    qs = packbuf;
    qd = unpackbuf;
    qsend = packbuf + packsize;
    qdstart = unpackbuf;
    qdend = unpackbuf + unpacksize;
    ulen = 0L;

    if (qs && packsize > 0 && unpacksize > 0)
    {
        {
            int             mostbits;
            int             i;
            int             bitnumtbl[16];
            unsigned int    deltatbl[16];
            unsigned int    cmptbl[16];
            unsigned char   codetbl[256];
            unsigned char   quickcodetbl[256];
            unsigned char   quicklentbl[256];

            bitsleft = -16;                                 /* init bit stream */
            bits = 0;
            SQgetbits(v,ZERO);

            SQgetbits(type,16);

            if (type&0x8000) /* 4 byte size field */
            {
                /* (skip nothing for 0x30fb) */
                if (type&0x100)                                 /* skip ulen */
                {
                    SQgetbits(v,16);
                    SQgetbits(v,16);
                }
                type &= ~0x100;

                SQgetbits(v,16);                                 /* unpack len */
                SQgetbits(ulen,16);
                ulen |= (v<<16);
                /* Check the length right away; it drives loops and buffer bounds below. */
                if (ulen < 0 || ulen > unpacksize)
                    overflow = 1;
            }
            else
            {
                /* (skip nothing for 0x30fb) */
                if (type&0x100)                                 /* skip ulen */
                {
                    SQgetbits(v,8);
                    SQgetbits(v,16);
                }
                type &= ~0x100;

                SQgetbits(v,8);                                 /* unpack len */
                SQgetbits(ulen,16);
                ulen |= (v<<16);
                /* see above */
                if (ulen < 0 || ulen > unpacksize)
                    overflow = 1;
            }

            {
                {
                    int numchars;

                    {
                        unsigned int basecmp;

                        {
                            unsigned int t;
                            SQgetbits(t,8);                          /* clue byte */
                            clue = static_cast<unsigned char>(t);
                        }

                        numchars = 0;
                        numbits = 1;
                        basecmp = static_cast<unsigned int>(0);

                        /* decode bitnums */

                        do
                        {
                            basecmp <<= 1;
                            deltatbl[numbits] = basecmp-numchars;

                            SQgetnum(bitnum);               /* # of codes of n bits */
                            /* at most 256 symbols in total; larger counts only come from a damaged stream */
                            if (bitnum < 0 || bitnum > 256 || numchars + bitnum > 256)
                            {
                                overflow = 1;
                                break;
                            }
                            bitnumtbl[numbits] = bitnum;

                            numchars += bitnum;
                            basecmp += bitnum;

                            cmp = 0;
                            if (bitnum)                             /* left justify cmp */
                                cmp = (basecmp << (16-numbits) & 0xffff);

                            cmptbl[numbits++] = cmp;
                            if (numbits >= 16)      /* bitnumtbl/deltatbl/cmptbl have 16 entries */
                            {
                                overflow = 1;
                                break;
                            }

                        }
                        while (!bitnum || cmp);                     /* n+1 bits in cmp? */
                    }
                    cmptbl[numbits-1] = 0xffffffff;               /* force match on most bits */

                    mostbits = numbits-1;

                    /* decode leapfrog code table */

                    {
                        signed char     leap[256];
                        unsigned char   nextchar;

                        SQmemset(leap,0,256);
                        nextchar = static_cast<unsigned char>(-1);

                        if (numchars < 0 || numchars > 256)     /* codetbl[256] */
                            overflow = 1;
                        for (i=0;i<numchars && !overflow;++i)
                        {
                            int leapdelta=0;
                            int leapGuard=0;

                            SQgetnum(leapdelta);
                            ++leapdelta;

                            /* `nextchar` is an unsigned char and wraps around. If all 256 entries
                               of `leap` are used, or `leapdelta` from the file is absurdly large,
                               the loop never decrements and spins forever. There cannot be more
                               than 256 free slots, so 256 passes per requested step suffice;
                               beyond that the stream is invalid. */
                            if (leapdelta < 1 || leapdelta > 256)
                            {
                                overflow = 1;
                                break;
                            }

                            do
                            {
                                if (++leapGuard > 256*257)
                                {
                                    overflow = 1;
                                    break;
                                }
                                ++nextchar;
                                if (!leap[nextchar])
                                    --leapdelta;
                            } while (leapdelta);

                            if (overflow)
                                break;

                            leap[nextchar] = 1;
                            codetbl[i] = nextchar;
                        }
                    }
                }

/****************************************************************/
/*  Make fast 8 tables                                          */
/****************************************************************/

                SQmemset(quicklentbl,64,256);

                {
                    int bits;
                    int bitnum;
                    int numbitentries;
                    int nextcode;
                    int nextlen;
                    int i;
                    unsigned char *codeptr;
                    unsigned char *quickcodeptr;
                    unsigned char *quicklenptr;

                    codeptr = codetbl;
                    quickcodeptr = quickcodetbl;
                    quicklenptr = quicklentbl;

                    for (bits=1; bits<=mostbits; ++bits)
                    {
                        bitnum = bitnumtbl[bits];
                        if (bits>=9)
                            break;
                        numbitentries = 1<<(8-bits);

                        while (bitnum--)
                        {
                            nextcode = *codeptr++;
                            nextlen = bits;
                            if (nextcode==clue)
                            {
                                cluelen = bits;
                                nextlen = 96;                   /* will force out of main loop */
                            }
                            for (i=0; i<numbitentries; ++i)
                            {
                                /* quickcodetbl/quicklentbl have 256 entries each */
                                if (quickcodeptr >= quickcodetbl+256 || quicklenptr >= quicklentbl+256)
                                {
                                    overflow = 1;
                                    break;
                                }
                                *quickcodeptr++ = static_cast<unsigned char>(nextcode);
                                *quicklenptr++ = static_cast<unsigned char>(nextlen);
                            }
                            if (overflow)
                                break;
                        }
                    }
                }
            }

/****************************************************************/
/*  Main decoder                                                */
/****************************************************************/

            for (;;)
            {
                unsigned char   *quickcodeptr = quickcodetbl;
                unsigned char   *quicklenptr  = quicklentbl;

/* quick 8 decode, then quick 8 fetch while fetching 16 bits would do it */

                for (;;)
                {
                    if (overflow)       /* bound violated -- leave the inner loop */
                        break;
                    numbits = quicklenptr[bits>>24];
                    bitsleft -= numbits;

                    if (bitsleft>=0)
                    {
                        do
                        {
                            PUTBYTE(qd) = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            PUTBYTE(qd) = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            PUTBYTE(qd) = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;
                            if (bitsleft<0) break;
                            PUTBYTE(qd) = quickcodeptr[bits>>24];
                            bits <<= numbits;

                            numbits = quicklenptr[bits>>24];
                            bitsleft -= numbits;

                        } while (bitsleft>=0);
                    }
                    bitsleft += 16;
                    if (bitsleft<0)
                        break;

/* quick 8 fetch */

                    PUTBYTE(qd) = quickcodeptr[bits>>24];
                    GET16BITS();
                    bits = bitsunshifted<<(16-bitsleft);
                }

                bitsleft = bitsleft-16+numbits;   /* back to normal */

/****************************************************************/
/*  16 bit decoder                                              */
/****************************************************************/

                {
                    unsigned char   code;

                    if (overflow)       /* leave the main loop */
                        break;

                    if (numbits!=96)
                    {
                        cmp = static_cast<unsigned int>(bits>>16);  /* 16 bit left justified compare */

                        numbits = 8;
                        do
                        {
                            ++numbits;
                            if (numbits >= 16)      /* cmptbl has 16 entries */
                            {
                                overflow = 1;
                                break;
                            }
                        }
                        while (cmp>=cmptbl[numbits]);
                        if (overflow) break;
                    }
                    else
                        numbits = cluelen;


                    cmp = bits >> (32-(numbits));
                    bits <<= (numbits);
                    bitsleft -= (numbits);

                    if (numbits < 0 || numbits >= 16) { overflow = 1; break; }
                    {
                        const unsigned int codeIndex = cmp - deltatbl[numbits];
                        if (codeIndex >= 256) { overflow = 1; break; }   /* codetbl[256] */
                        code = codetbl[codeIndex];  /* the code */
                    }

                    if (code!=clue && bitsleft>=0)
                    {
                        PUTBYTE(qd) = code;
                        continue;
                    }

                    if (bitsleft<0)
                    {
                        GET16BITS();
                        bits = bitsunshifted<<-bitsleft;
                        bitsleft += 16;
                    }

                    if (code!=clue)
                    {
                        PUTBYTE(qd) = code;
                        continue;
                    }

                    /* handle clue */

                    {
                        int    runlen=0;
                        unsigned char *d=qd;
                        unsigned char *dest;

                        SQgetnum(runlen);
                        if (runlen)                             /* runlength sequence */
                        {
                            /* The run must fit into the destination buffer, and there must be
                               a preceding byte to repeat. */
                            if (runlen < 0 || d <= qdstart || (qdend - d) < runlen)
                            {
                                overflow = 1;
                                break;
                            }
                            dest = d+runlen;
                            code = *(d-1);
                            do
                            {
                                PUTBYTE(d) = code;
                            } while (d<dest);

                            qd = d;
                            continue;
                        }
                    }

                    SQgetbits(v,1);                         /* End Of File */
                    if (v)
                        break;

                    {
                        unsigned int t;
                        SQgetbits(t,8);                    /* explicite byte */
                        code = static_cast<unsigned char>(t);
                    }
                    PUTBYTE(qd) = code;
                    continue;
                }

            }


/****************************************************************/
/*  Undelta                                                     */
/****************************************************************/

            {
                /* unsigned: the sums wrap over long buffers, only the low byte is used */
                unsigned int i;
                unsigned int nextchar;

                if (type==0x32fb || type==0xb2fb)                           /* deltaed? */
                {
                    i = 0;
                    qd = unpackbuf;
                    while (qd<unpackbuf+ulen && qd<qdend)    /* never past the buffer */
                    {
                        i += static_cast<unsigned int>(*qd);
                        PUTBYTE(qd) = static_cast<unsigned char>(i);
                    }
                }
                else if (type==0x34fb || type==0xb4fb)                      /* accelerated? */
                {

                    i = 0;
                    nextchar = 0;
                    qd = unpackbuf;
                    while (qd<unpackbuf+ulen && qd<qdend)    /* never past the buffer */
                    {
                        i += static_cast<unsigned int>(*qd);
                        nextchar += i;
                        PUTBYTE(qd) = static_cast<unsigned char>(nextchar);
                    }
                }
            }
        }
    }

    /* Any bound violation makes the stream invalid, as does a decoded size that differs from
       the announced length. */
    if (overflow || ulen < 0 || ulen > unpacksize || static_cast<int>(qd - qdstart) != ulen)
        return(0);

    return(ulen);
}


#undef PUTBYTE
#undef GET16BITS
#undef HUFF_MAX_PAD_BYTES
#undef SQGETNUM_MAX_N

int GCALL HUFF_decode_bounded(void *dest, int destsize,
                              const void *compresseddata, int compresseddatasize,
                              int *compressedsize)
{
    int ulen;

    if (!dest || !compresseddata || destsize <= 0 || compresseddatasize <= 0)
    {
        if (compressedsize)
            *compressedsize = 0;
        return(0);
    }

    ulen = HUFF_decompress_bounded((unsigned char *)compresseddata, compresseddatasize,
                                   static_cast<unsigned char *>(dest), destsize);

    if (compressedsize)
        *compressedsize = compresseddatasize;   /* the decoder does not report the exact length */

    return(ulen);
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 30fb..35fb header */

bool GCALL HUFF_is(const void *compresseddata)
{
    bool ok=false;
    int packtype=ggetm(compresseddata,2);

    if (packtype==0x30fb
     || packtype==0x31fb
     || packtype==0x32fb
     || packtype==0x33fb
     || packtype==0x34fb
     || packtype==0x35fb
     || packtype==0xb0fb
     || packtype==0xb1fb
     || packtype==0xb2fb
     || packtype==0xb3fb
     || packtype==0xb4fb
     || packtype==0xb5fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL HUFF_size(const void *compresseddata)
{
    int len=0;
    int packtype=ggetm(compresseddata,2);
    int ssize=(packtype&0x8000)?4:3;

    if (packtype&0x100)     /* 31fb 33fb 35fb */
    {
        len = ggetm((char *)compresseddata+2+ssize,ssize);
    }
    else                    /* 30fb 32fb 34fb */
    {
        len = ggetm((char *)compresseddata+2,ssize);
    }

    return(len);
}

int GCALL HUFF_decode(void *dest, const void *compresseddata, int *compressedsize)
{
    return(HUFF_decompress((unsigned char *)compresseddata, static_cast<unsigned char *>(dest)));
}

#endif

