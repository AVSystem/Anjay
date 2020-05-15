/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AVS_COAP_POISON_H
#define AVS_COAP_POISON_H

// This file ensures that some functions we "don't like" from the standard
// library (for reasons described below) are not used in any of the source
// files. This file is included only when compiling using GCC
//
// Also note that some functions (such as time()) are blacklisted with whole
// headers, through test_headers.py.

#include <avsystem/commons/avs_defs.h>

// STDIO ///////////////////////////////////////////////////////////////////////

// Forward inclusion of standard headers, before poisoning all of their names
#include <stdio.h>
#include <stdlib.h>

#undef stdin
#undef stdout
#undef stderr
#undef getc
#undef putc

#pragma GCC poison fclose
#pragma GCC poison fflush
#pragma GCC poison fgetc
#pragma GCC poison fgetpos
#pragma GCC poison fgets
#pragma GCC poison fgetwc
#pragma GCC poison fgetws
#pragma GCC poison fopen
#pragma GCC poison fprintf
#pragma GCC poison fputc
#pragma GCC poison fputs
#pragma GCC poison fputwc
#pragma GCC poison fputws
#pragma GCC poison freopen
#pragma GCC poison fscanf
#pragma GCC poison fsetpos
#pragma GCC poison fwprintf
#pragma GCC poison fwrite
#pragma GCC poison fwscanf
#pragma GCC poison getc
#pragma GCC poison getchar
#pragma GCC poison gets
#pragma GCC poison getwc
#pragma GCC poison getwchar
#pragma GCC poison perror
#pragma GCC poison printf
#pragma GCC poison putc
#pragma GCC poison putchar
#pragma GCC poison puts
#pragma GCC poison putwc
#pragma GCC poison putwchar
#pragma GCC poison remove
#pragma GCC poison rename
#pragma GCC poison rewind
#pragma GCC poison scanf
#pragma GCC poison setbuf
#pragma GCC poison setvbuf
#pragma GCC poison stderr
#pragma GCC poison stdin
#pragma GCC poison stdout
#pragma GCC poison tmpfile
#pragma GCC poison tmpnam
#pragma GCC poison ungetc
#pragma GCC poison ungetwc
#pragma GCC poison vfprintf
#pragma GCC poison vfscanf
#pragma GCC poison vfwprintf
#pragma GCC poison vfwscanf
#pragma GCC poison vprintf
#pragma GCC poison vscanf
#pragma GCC poison vwprintf
#pragma GCC poison vwscanf
#pragma GCC poison wprintf
#pragma GCC poison wscanf

#pragma GCC poison malloc
#pragma GCC poison calloc
#pragma GCC poison realloc
#pragma GCC poison free

#pragma GCC poison atexit
#pragma GCC poison exit
#pragma GCC poison getenv
#pragma GCC poison abort

// System program control flow functions
#pragma GCC poison _Exit
#pragma GCC poison system

// Default (and not thread-safe) PRNG
#pragma GCC poison rand
#pragma GCC poison srand

// multibyte character conversions
#pragma GCC poison mblen
#pragma GCC poison mbtowc
#pragma GCC poison mbstowc
#pragma GCC poison wcstombs
#pragma GCC poison wctomb

#endif /* AVS_COAP_POISON_H */
