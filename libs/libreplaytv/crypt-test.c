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

#include <stdio.h>
#include <string.h>

#ifdef __unix__
#include <unistd.h>
#endif

#include "rtv.h"
#include "rtvlib.h"
#include "crypt.h"

static int run_crypt_test(unsigned char * cyphertext1, size_t ctext_len,
                int checksum_num) 
{
    unsigned char plaintext1[1024];
    unsigned char cyphertext2[1024];
    unsigned char plaintext2[1024];
    __u32  plain_length, cypher_length;
    __u32  t;
    __u32  r;

    memset(plaintext1, 0, sizeof plaintext1);
    memset(cyphertext2, 0, sizeof cyphertext2);
    memset(plaintext2, 0, sizeof plaintext2);
    
    r = rtv_decrypt(cyphertext1, ctext_len,
                    plaintext1, sizeof plaintext1,
                    &t, &plain_length, checksum_num);
    if (r != 0) {
        RTV_PRT("error decrypting original!\n");
        return -1;
    }
    
    RTV_PRT("1st plaintext: :%.*s: (length=%ld, timestamp=%ld)\n",
           (int)plain_length, plaintext1, plain_length, t);
    r = rtv_encrypt(plaintext1, plain_length,
                    cyphertext2, sizeof cyphertext2,
                    &cypher_length, checksum_num);
    if (r != 0) {
        RTV_PRT("error re-encrypting\n");
        return -1;
    }

    r = rtv_decrypt(cyphertext2, cypher_length,
                    plaintext2, sizeof plaintext2,
                    &t, &plain_length, checksum_num);
    if (r != 0) {
        RTV_PRT("error decrypting original!\n");
        return -1;
    }
    RTV_PRT("2nd plaintext: :%.*s: (length=%ld, timestamp=%ld)\n",
           (int)plain_length, plaintext2, plain_length, t);
    
    return strcmp(plaintext1, plaintext2);
}

int rtv_crypt_test(void)
{
    unsigned char cyphertext1[] = {
        0x7d, 0xd9, 0xc3, 0x54, 0xec, 0x01, 0x14, 0x73,
        0x10, 0x3f, 0x0a, 0xc5, 0x99, 0x35, 0x12, 0x1e,
        0x08, 0xaf, 0xd5, 0xc7, 0x1a, 0xb3, 0xfe, 0x80,
        0xa7, 0x53, 0x72, 0x0d, 0xc9, 0x76, 0xf1, 0x08,
        0x6c, 0xff, 0xa3, 0xb9, 0x25, 0xcc, 0xed, 0xc1,
        0x10, 0x41, 0xdd, 0x5d, 0x04, 0xea, 0xcb, 0xf9,
        0x70, 0x79, 0x3f, 0x74, 0x61, 0x9a, 0x6d, 0xdb,
        0x17, 0x58, 0x10, 0x10, 0x45, 0xa4, 0x0c
    }; // hourly report

    unsigned char cyphertext2[] = {
        0x23, 0xaf, 0xcb, 0x48, 0x0b, 0x00, 0x00, 0x6e,
        0x2e, 0xf0, 0x47, 0x1a, 0xb9, 0xa2, 0x32, 0xae,
        0xf8, 0xf7, 0x06, 0xf6, 0xa1, 0xa1, 0xbc, 0xc7,
        0x0a, 0xce, 0xdf, 0x10, 0x05, 0xc0, 0xc8, 0x1d,
        0x46, 0x30, 0x8d, 0xbc, 0x25, 0xce, 0xa6, 0x00,
        0x6c, 0x14, 0xaf
    }; // maybe-bad 4.3 query string

    RTV_PRT("\nTesting checksum 0, RDDNS hourly update\n");
    if ( run_crypt_test(cyphertext1, sizeof cyphertext1, 0) != 0 ) {
       RTV_PRT("Test #1 FAILED\n");
    }
    else {
       RTV_PRT("Test #1 PASSED\n");
    }

    RTV_PRT("\nTesting checksum 1, self-generated 4.3 query string\n");
    if ( run_crypt_test(cyphertext2, sizeof cyphertext2, 1) != 0 ) {
       RTV_PRT("Test #2 FAILED\n");
    }
    else {
       RTV_PRT("Test #2 PASSED\n");
    }

    return (0);
}
