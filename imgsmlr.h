/*-------------------------------------------------------------------------
 *
 *          Image similarity extension
 *
 * Copyright (c) 2013, PostgreSQL Global Development Group
 *
 * This software is released under the PostgreSQL Licence.
 *
 * Author: Alexander Korotkov <aekorotkov@gmail.com>
 *
 * IDENTIFICATION
 *    imgsmlr/imgsmlr.h
 *-------------------------------------------------------------------------
 */
#ifndef IMGSMLR_H
#define IMGSMLR_H

#define PATTERN_SIZE 64
#define SIGNATURE_SIZE 16

typedef struct
{
	float values[PATTERN_SIZE][PATTERN_SIZE];
} PatternData;

typedef struct
{
	char		vl_len_[4];		/* Do not touch this field directly! */
	PatternData	data;
} Pattern;

typedef struct
{
	float values[SIGNATURE_SIZE];
} Signature;

#define CHECK_SIGNATURE_KEY(key) Assert(VARSIZE_ANY_EXHDR(key) == sizeof(Signature) || VARSIZE_ANY_EXHDR(key) == 2 * sizeof(Signature));

#endif   /* IMGSMLR_H */
