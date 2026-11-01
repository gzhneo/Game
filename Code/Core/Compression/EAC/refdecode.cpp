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

#ifndef __REFREAD
#define __REFREAD 1

#include <string.h>
#include "codex.h"
#include "refcodex.h"

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 10fb header */

bool GCALL REF_is(const void *compresseddata)
{
    bool ok=false;
    int packtype=ggetm(compresseddata,2);

    if (packtype==0x10fb
     || packtype==0x11fb
     || packtype==0x90fb
     || packtype==0x91fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL REF_size(const void *compresseddata)
{
    int len=0;
    int packtype=ggetm(compresseddata,2);
    int ssize=(packtype&0x8000)?4:3;

    if (packtype&0x100)     /* 11fb */
    {
        len = ggetm((char *)compresseddata+2+ssize,ssize);
    }
    else                    /* 10fb */
    {
        len = ggetm((char *)compresseddata+2,ssize);
    }

    return(len);
}


/****************************************************************/
/*  Bounded decoding                                            */
/****************************************************************/
/*
** REF_decode below checks nothing: it follows the lengths and back references in the stream
** until the stream says "end". The stream always comes from a file (.big archives, replays,
** save games), so a single flipped bit can write past the destination or read past the source.
**
** This version produces the same output but checks before each step:
**   * source: no byte past `compresseddata + compresseddatasize`,
**   * destination: no byte past `dest + destsize`,
**   * back references: no pointer before `dest`.
** On a violation it stops and returns 0 instead of overwriting memory.
**
** For valid data the result is byte for byte the same as REF_decode.
*/
int GCALL REF_decode_bounded(void *dest, int destsize,
                             const void *compresseddata, int compresseddatasize,
                             int *compressedsize)
{
    const unsigned char *s;
    const unsigned char *send;
    unsigned char *ref;
    unsigned char *d;
    unsigned char *dstart;
    unsigned char *dend;
    unsigned char first;
    unsigned char second;
    unsigned char third;
    unsigned char forth;
    unsigned int  run;
    unsigned int  type;
    int          ulen = 0;
    int          ok = 1;

    if (!dest || !compresseddata || destsize <= 0 || compresseddatasize <= 0)
    {
        if (compressedsize)
            *compressedsize = 0;
        return(0);
    }

    s      = static_cast<const unsigned char *>(compresseddata);
    send   = s + compresseddatasize;
    d      = static_cast<unsigned char *>(dest);
    dstart = d;
    dend   = d + destsize;

    /* header: type and length */
    if (send - s < 2)
        ok = 0;
    if (ok)
    {
        type = *s++;
        type = (type<<8) + *s++;

        if (type&0x8000)                              /* 4 byte size field */
        {
            if (type&0x100)                           /* skip ulen */
                s += 4;
            if (send - s < 4)
                ok = 0;
            else
            {
                ulen = *s++;
                ulen = (ulen<<8) + *s++;
                ulen = (ulen<<8) + *s++;
                ulen = (ulen<<8) + *s++;
            }
        }
        else
        {
            if (type&0x100)                           /* skip ulen */
                s += 3;
            if (send - s < 3)
                ok = 0;
            else
            {
                ulen = *s++;
                ulen = (ulen<<8) + *s++;
                ulen = (ulen<<8) + *s++;
            }
        }
    }

    /* The announced length must fit into the destination buffer. */
    if (ok && (ulen < 0 || ulen > destsize))
        ok = 0;

    while (ok)
    {
        if (s >= send)                                /* stream ends without end command */
        {
            ok = 0;
            break;
        }
        first = *s++;

        if (!(first&0x80))                            /* short form */
        {
            if (s >= send) { ok = 0; break; }
            second = *s++;
            run = first&3;
            if (static_cast<unsigned int>(send - s) < run || static_cast<unsigned int>(dend - d) < run) { ok = 0; break; }
            while (run--)
                *d++ = *s++;
            ref = d-1 - (((first&0x60)<<3) + second);
            run = ((first&0x1c)>>2)+3-1;
            if (ref < dstart || static_cast<unsigned int>(dend - d) < run + 1) { ok = 0; break; }
            do
            {
                *d++ = *ref++;
            } while (run--);
            continue;
        }
        if (!(first&0x40))                            /* int form */
        {
            if (send - s < 2) { ok = 0; break; }
            second = *s++;
            third = *s++;
            run = second>>6;
            if (static_cast<unsigned int>(send - s) < run || static_cast<unsigned int>(dend - d) < run) { ok = 0; break; }
            while (run--)
                *d++ = *s++;

            ref = d-1 - (((second&0x3f)<<8) + third);

            run = (first&0x3f)+4-1;
            if (ref < dstart || static_cast<unsigned int>(dend - d) < run + 1) { ok = 0; break; }
            do
            {
                *d++ = *ref++;
            } while (run--);
            continue;
        }
        if (!(first&0x20))                            /* very int form */
        {
            if (send - s < 3) { ok = 0; break; }
            second = *s++;
            third = *s++;
            forth = *s++;
            run = first&3;
            if (static_cast<unsigned int>(send - s) < run || static_cast<unsigned int>(dend - d) < run) { ok = 0; break; }
            while (run--)
                *d++ = *s++;

            ref = d-1 - (((first&0x10)>>4<<16) +  (second<<8) + third);

            run = ((first&0x0c)>>2<<8) + forth + 5-1;
            if (ref < dstart || static_cast<unsigned int>(dend - d) < run + 1) { ok = 0; break; }
            do
            {
                *d++ = *ref++;
            } while (run--);
            continue;
        }

        run = ((first&0x1f)<<2)+4;                    /* literal */
        if (run<=112)
        {
            if (static_cast<unsigned int>(send - s) < run || static_cast<unsigned int>(dend - d) < run) { ok = 0; break; }
            while (run--)
                *d++ = *s++;
            continue;
        }

        run = first&3;                                /* eof (+0..3 literal) */
        if (static_cast<unsigned int>(send - s) < run || static_cast<unsigned int>(dend - d) < run) { ok = 0; break; }
        while (run--)
            *d++ = *s++;
        break;                                        /* regular end */
    }

    if (compressedsize)
        *compressedsize = static_cast<int>(s - static_cast<const unsigned char *>(compresseddata));

    if (!ok)
        return(0);

    /* The decoded amount must match the announced length. */
    if (static_cast<int>(d - dstart) != ulen)
        return(0);

    return(ulen);
}


int GCALL REF_decode(void *dest, const void *compresseddata, int *compressedsize)
{
    unsigned char *s;
    unsigned char *ref;
    unsigned char *d;
    unsigned char first;
    unsigned char second;
    unsigned char third;
    unsigned char forth;
    unsigned int  run;
    unsigned int  type;
    int          ulen;

    s = (unsigned char *) compresseddata;
    d = static_cast<unsigned char *>(dest);
    ulen = 0L;

    if (s)
    {
        type = *s++;
        type = (type<<8) + *s++;

        if (type&0x8000) /* 4 byte size field */
        {
            if (type&0x100)                       /* skip ulen */
                s += 4;

            ulen = *s++;
            ulen = (ulen<<8) + *s++;
            ulen = (ulen<<8) + *s++;
            ulen = (ulen<<8) + *s++;
        }
        else
        {
            if (type&0x100)                       /* skip ulen */
                s += 3;

            ulen = *s++;
            ulen = (ulen<<8) + *s++;
            ulen = (ulen<<8) + *s++;
        }

        for (;;)
        {
            first = *s++;
            if (!(first&0x80))          /* short form */
            {
                second = *s++;
                run = first&3;
                while (run--)
                    *d++ = *s++;
                ref = d-1 - (((first&0x60)<<3) + second);
                run = ((first&0x1c)>>2)+3-1;
                do
                {
                    *d++ = *ref++;
                } while (run--);
                continue;
            }
            if (!(first&0x40))          /* int form */
            {
                second = *s++;
                third = *s++;
                run = second>>6;
                while (run--)
                    *d++ = *s++;

                ref = d-1 - (((second&0x3f)<<8) + third);

                run = (first&0x3f)+4-1;
                do
                {
                    *d++ = *ref++;
                } while (run--);
                continue;
            }
            if (!(first&0x20))          /* very int form */
            {
                second = *s++;
                third = *s++;
                forth = *s++;
                run = first&3;
                while (run--)
                    *d++ = *s++;

                ref = d-1 - (((first&0x10)>>4<<16) +  (second<<8) + third);

                run = ((first&0x0c)>>2<<8) + forth + 5-1;
                do
                {
                    *d++ = *ref++;
                } while (run--);
                continue;
            }
            run = ((first&0x1f)<<2)+4;  /* literal */
            if (run<=112)
            {
                while (run--)
                    *d++ = *s++;
                continue;
            }
            run = first&3;              /* eof (+0..3 literal) */
            while (run--)
                *d++ = *s++;
            break;
        }
    }
    if (compressedsize)
        *compressedsize = static_cast<int>(reinterpret_cast<char *>(s)-(char *)compresseddata);
    return(ulen);
}

#endif

