/*
 * Copyright (C) 2004 John Honeycutt
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include "rtv.h"
#include "rtvlib.h"
#include "bigfile.h"

#ifdef WIN32
#define BIGFILE_BY_WINDOWS_UNIXISH_LL 1
#endif

#if BIGFILE_BY_STDIO
#include <errno.h>

struct big_file 
{
    FILE * fp;
};

BIGFILE * bfopen(const char * filename, const char * mode)
{
    BIGFILE * stream;

    stream = malloc(sizeof *stream);
    if (!stream)
        return NULL;
    stream->fp = fopen(filename, mode);
    if (!stream->fp) {
        int e = errno;
        free(stream);
        errno = e;
        return NULL;
    }
    return stream;
}

BIGFILE * bfreopen(FILE *fp)
{
    BIGFILE * stream;

    stream = malloc(sizeof *stream);
    if (!stream)
        return NULL;
    stream->fp = fp;
    return stream;
}

int bfseek(BIGFILE * stream, __s64 offset, int whence)
{
    return fseeko(stream->fp, offset, whence);
}

__u64 bftell(BIGFILE * stream)
{
    return ftello(stream->fp);
}

size_t bfread(void * ptr, size_t size, size_t nmemb, BIGFILE * stream)
{
    return fread(ptr, size, nmemb, stream->fp);
}

size_t bfwrite(const void * ptr, size_t size, size_t nmemb, BIGFILE * stream)
{
    return fwrite(ptr, size, nmemb, stream->fp);
}

int bfclose(BIGFILE *stream)
{
    int r, e;

    r = fclose(stream->fp);
    e = errno;
    free(stream);
    errno = e;

    return r;
}
#else
#ifdef BIGFILE_BY_WINDOWS_UNIXISH_LL
#include <IO.H>
#include <SYS\TYPES.H>
#include <FCNTL.H>
#include <SYS\STAT.H>

struct big_file
{
    int fd;
};

BIGFILE * bfopen(const char * filename, const char * mode)
{
    BIGFILE * stream;
    int flag = 0;
    
    stream = malloc(sizeof *stream);
    if (!stream)
        return NULL;
    if (strcmp(mode, "r") == 0) {
        flag |= _O_RDONLY;
    } else if (strcmp(mode, "w") == 0) {
        flag |= _O_WRONLY | _O_CREAT;
    } else if (strcmp(mode, "w+") == 0) {
        flag |= _O_RDWR | _O_CREAT;
    } else {
        RTV_ERRLOG("bfopen: mode '%s' not implented\n", mode);
        flag = O_RDWR;
    }
    flag |= _O_BINARY;
    
    stream->fd = _open(filename, flag, _S_IREAD | _S_IWRITE);
    if (stream->fd < 0) {
        int e = errno;
        free(stream);
        errno = e;
        return NULL;
    }
    return stream;
}

BIGFILE * bfreopen(FILE *fp)
{
    BIGFILE * stream;

    stream = malloc(sizeof *stream);
    if (!stream)
        return NULL;
    stream->fd = _fileno(fp);
    return stream;
}
    
int bfseek(BIGFILE * stream, s64 offset, int whence)
{
    __int64 r;

    r = _lseeki64(stream->fd, offset, whence);
    if (r < 0)
        return -1;
    return 0;
}

u64 bftell(BIGFILE * stream)
{
    return _telli64(stream->fd);
}

size_t bfread(void * ptr, size_t size, size_t nmemb, BIGFILE * stream)
{
    size_t m;
    unsigned char * p = ptr;
    
    for (m = 0; m < nmemb; m++) {
        if (_read(stream->fd, p, size) != size)
            return m;
        p += size;
    }
    return m;
}

size_t bfwrite(const void * ptr, size_t size, size_t nmemb, BIGFILE * stream)
{
    size_t m;
    unsigned char * p = ptr;
    
    for (m = 0; m < nmemb; m++) {
        if (_write(stream->fd, p, size) != size)
            return m;
        p += size;
    }
    return m;
}

int bfclose(BIGFILE *stream)
{
    int r, e;

    r = _close(stream->fd);
    e = errno;
    free(stream);
    errno = e;

    return r;
}
#else
#error "You need to choose a BIGFILE implementation or write one"
#endif
#endif
