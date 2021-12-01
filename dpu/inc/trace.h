/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef TRACE_H_
#define TRACE_H_

//#define TRACE

#ifdef TRACE

#include <stdio.h>
#define PRINT printf

#else

#define PRINT(...)

#endif

#endif /* TRACE_H_ */
