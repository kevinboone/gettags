/*==========================================================================
gettags
types.h
Copyright (c)2012-2018 Kevin Boone
Distributed under the terms of the GNU Public Licence, v3.0

I've separated these basic types into a separate file because if you 
want to use tag_reader.c in some other application, these are probably
already defined somewhere else
==========================================================================*/
#pragma once

#ifndef BOOL
typedef int BOOL;
#endif

#ifndef BYTE
typedef unsigned char BYTE;
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0 
#endif
