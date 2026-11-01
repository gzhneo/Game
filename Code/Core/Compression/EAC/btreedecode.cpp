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

#ifndef __BTRREAD
#define __BTRREAD 1

#include <string.h>
#include "codex.h"
#include "btreecodex.h"

/****************************************************************/
/*  Internal Functions                                          */
/****************************************************************/

struct BTreeDecodeContext
{
    signed char    cluetbl[256];
    unsigned char  left[256];
    unsigned char  right[256];
    unsigned char  *d;
};

static void BTREE_chase(struct BTreeDecodeContext *DC, unsigned char node)
{
    if (DC->cluetbl[node])
    {
        BTREE_chase(DC,DC->left[node]);
        BTREE_chase(DC,DC->right[node]);
        return;
    }
    *DC->d++ = node;
}

static int BTREE_decompress(unsigned char *packbuf,unsigned char *unpackbuf)
{
    int  node;
    int  i;
    int  nodes;
    int  clue;
    int ulen;
    unsigned char *s;
    signed char c;
    unsigned int type;
    struct BTreeDecodeContext DC;

    s = packbuf;
    DC.d = unpackbuf;
    ulen = 0L;

    if (s)
    {
        type = ggetm(s,2);
        s += 2;

        /* (skip nothing for 0x46fb) */
        if (type==0x47fb)                       /* skip ulen */
            s += 3;

        ulen = ggetm(s,3);
        s += 3;

        for (i=0;i<256;++i)                     /* 0 means a code is a leaf */
            DC.cluetbl[i] = 0;

        clue = *s++;
        DC.cluetbl[clue] = 1;                   /* mark clue as special */

        nodes = *s++;
        for (i=0;i<nodes;++i)
        {   node = *s++;
            DC.left[node]  = *s++;
            DC.right[node] = *s++;
            DC.cluetbl[node] = static_cast<signed char>(-1);
        }

        for (;;)
        {
            node = static_cast<int>(*s++);
            c=DC.cluetbl[node];
            if (!c)
            {
                *DC.d++ = static_cast<unsigned char>(node);
                continue;
            }
            if (c<0)
            {
                BTREE_chase(&DC,DC.left[node]);
                BTREE_chase(&DC,DC.right[node]);
                continue;
            }
            node = static_cast<int>(*s++);
            if (node)
            {
                *DC.d++ = static_cast<char>(node);
                continue;
            }
            break;
        }
    }
    return(ulen);
}

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 46fb header */

bool GCALL BTREE_is(const void *compresseddata)
{
    bool ok=false;

    if (ggetm(compresseddata,2)==0x46fb
     || ggetm(compresseddata,2)==0x47fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL BTREE_size(const void *compresseddata)
{
    int len=0;

    if (ggetm(compresseddata,2)==0x46fb)
    {
        len = ggetm((char *)compresseddata+2,3);
    }
    else
    {
        len = ggetm((char *)compresseddata+2+3,3);
    }

    return(len);
}

/****************************************************************/
/*  Bounded decoding                                            */
/****************************************************************/
/*
** Like REF_decode, the decoder below follows the stream without any checks. BTREE_chase walks
** tree nodes taken from the file: a cycle makes the recursion endless and writes past the end
** of the destination buffer.
**
** This version checks source, destination and recursion depth. For valid data it gives the
** same result; for invalid data it returns 0.
*/
struct BTreeBoundedContext
{
    struct BTreeDecodeContext base;
    unsigned char           *dend;       /* first byte past the destination buffer */
    int                      overflow;   /* 1 = bound violated */
};

/* Maximum recursion depth: the tree has at most 256 nodes, so a valid derivation cannot be
   deeper; anything beyond is a cycle in the file data. */
constexpr auto BTREE_MAX_CHASE_DEPTH = 256;

static void BTREE_chase_bounded(struct BTreeBoundedContext *BC, unsigned char node, int depth)
{
    if (BC->overflow)
        return;
    if (depth > BTREE_MAX_CHASE_DEPTH)
    {
        BC->overflow = 1;
        return;
    }
    if (BC->base.cluetbl[node])
    {
        BTREE_chase_bounded(BC, BC->base.left[node], depth+1);
        BTREE_chase_bounded(BC, BC->base.right[node], depth+1);
        return;
    }
    if (BC->base.d >= BC->dend)
    {
        BC->overflow = 1;
        return;
    }
    *BC->base.d++ = node;
}

int GCALL BTREE_decode_bounded(void *dest, int destsize,
                               const void *compresseddata, int compresseddatasize,
                               int *compressedsize)
{
    struct BTreeBoundedContext BC;
    const unsigned char *s;
    const unsigned char *send;
    unsigned char *dstart;
    int   node;
    int   i;
    int   nodes;
    int   clue;
    int   ulen = 0;
    unsigned int type;

    if (!dest || !compresseddata || destsize <= 0 || compresseddatasize <= 0)
    {
        if (compressedsize)
            *compressedsize = 0;
        return(0);
    }

    s    = static_cast<const unsigned char *>(compresseddata);
    send = s + compresseddatasize;
    dstart = static_cast<unsigned char *>(dest);

    BC.base.d = dstart;
    BC.dend   = dstart + destsize;
    BC.overflow = 0;

    if (send - s < 5)
        return(0);

    type = ggetm(s,2);
    s += 2;

    if (type==0x47fb)                       /* skip ulen */
        s += 3;
    if (send - s < 3)
        return(0);

    ulen = ggetm(s,3);
    s += 3;
    if (ulen < 0 || ulen > destsize)
        return(0);

    for (i=0;i<256;++i)                     /* 0 means a code is a leaf */
        BC.base.cluetbl[i] = 0;

    if (send - s < 2)
        return(0);
    clue = *s++;
    BC.base.cluetbl[clue] = 1;              /* mark clue as special */

    nodes = *s++;
    if (send - s < nodes*3)
        return(0);
    for (i=0;i<nodes;++i)
    {   node = *s++;
        BC.base.left[node]  = *s++;
        BC.base.right[node] = *s++;
        BC.base.cluetbl[node] = static_cast<signed char>(-1);
    }

    for (;;)
    {
        if (s >= send || BC.overflow)
            return(0);

        node = static_cast<int>(*s++);
        if (!BC.base.cluetbl[node])
        {
            if (BC.base.d >= BC.dend)
                return(0);
            *BC.base.d++ = static_cast<unsigned char>(node);
            continue;
        }
        if (BC.base.cluetbl[node] < 0)
        {
            BTREE_chase_bounded(&BC, BC.base.left[node], 0);
            BTREE_chase_bounded(&BC, BC.base.right[node], 0);
            continue;
        }
        if (s >= send)
            return(0);
        node = static_cast<int>(*s++);
        if (node)
        {
            if (BC.base.d >= BC.dend)
                return(0);
            *BC.base.d++ = static_cast<unsigned char>(node);
            continue;
        }
        break;                              /* regular end */
    }

    if (compressedsize)
        *compressedsize = static_cast<int>(s - static_cast<const unsigned char *>(compresseddata));

    if (BC.overflow || static_cast<int>(BC.base.d - dstart) != ulen)
        return(0);

    return(ulen);
}


int GCALL BTREE_decode(void *dest, const void *compresseddata, int *compressedsize)
{
    return(BTREE_decompress((unsigned char *)compresseddata,static_cast<unsigned char *>(dest)));
}

#endif

