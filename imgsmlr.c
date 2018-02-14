/*-------------------------------------------------------------------------
 *
 *          Image similarity extension
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * This software is released under the PostgreSQL Licence.
 *
 * Author: Alexander Korotkov <aekorotkov@gmail.com>
 *
 * IDENTIFICATION
 *    imgsmlr/imgsmlr.c
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "c.h"
#include "fmgr.h"
#include "imgsmlr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include <gd.h>
#include <stdio.h>
#include <math.h>

#define DEBUG_PRINT

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jpeg2pattern);
Datum		jpeg2pattern(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(png2pattern);
Datum		png2pattern(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(gif2pattern);
Datum		gif2pattern(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pattern2signature);
Datum		pattern2signature(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pattern_in);
Datum		pattern_in(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pattern_out);
Datum		pattern_out(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(signature_in);
Datum		signature_in(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(signature_out);
Datum		signature_out(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pattern_distance);
Datum		pattern_distance(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(signature_distance);
Datum		signature_distance(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(shuffle_pattern);
Datum		shuffle_pattern(PG_FUNCTION_ARGS);

static Pattern *image2pattern(gdImagePtr im);
static void makePattern(gdImagePtr im, PatternData *pattern);
static void normalizePattern(PatternData *pattern);
static void waveletTransform(PatternData *dst, PatternData *src, int size);
static float calcSumm(PatternData *pattern, int x, int y, int sX, int sY);
static void calcSignature(PatternData *pattern, Signature *signature);
static float calcDiff(PatternData *patternA, PatternData *patternB, int x, int y, int sX, int sY);
static void shuffle(PatternData *dst, PatternData *src, int x, int y, int sX, int sY, int w);
static float read_float(char **s, char *type_name, char *orig_string);

#ifdef DEBUG_INFO
static void debugPrintPattern(PatternData *pattern, const char *filename, bool color);
static void debugPrintSignature(Signature *signature, const char *filename);
#endif

/*
 * Transform GD image into pattern.
 */
static Pattern *
image2pattern(gdImagePtr im)
{
	gdImagePtr	tb;
	Pattern *pattern;
	PatternData source;

	/* Resize image */
	tb = gdImageCreateTrueColor(PATTERN_SIZE, PATTERN_SIZE);
	if (!tb)
	{
		elog(NOTICE, "Error creating pattern");
		return NULL;
	}
	gdImageCopyResampled(tb, im, 0, 0, 0, 0, PATTERN_SIZE, PATTERN_SIZE,
			im->sx, im->sy);

	/* Create source pattern as greyscale image */
	makePattern(tb, &source);
	gdImageDestroy(tb);

#ifdef DEBUG_INFO
	debugPrintPattern(&source, "/tmp/pattern1.raw", false);
#endif

	/* "Normalize" intensiveness in the pattern */
	normalizePattern(&source);

#ifdef DEBUG_INFO
	debugPrintPattern(&source, "/tmp/pattern2.raw", false);
#endif

	/* Allocate pattern */
	pattern = (Pattern *)palloc(sizeof(Pattern));
	SET_VARSIZE(pattern, sizeof(Pattern));

	/* Do wavelet transform */
	waveletTransform(&pattern->data, &source, PATTERN_SIZE);

#ifdef DEBUG_INFO
	debugPrintPattern(transformed, "/tmp/pattern3.raw", true);
#endif

	return pattern;
}

/*
 * Load pattern from jpeg image in bytea.
 */
Datum
jpeg2pattern(PG_FUNCTION_ARGS)
{
	bytea *img = PG_GETARG_BYTEA_P(0);
	Pattern *pattern;
	gdImagePtr im;

	im = gdImageCreateFromJpegPtr(VARSIZE_ANY_EXHDR(img), VARDATA_ANY(img));
	PG_FREE_IF_COPY(img, 0);
	if (!im)
	{
		elog(NOTICE, "Error loading jpeg");
		PG_RETURN_NULL();
	}
	pattern = image2pattern(im);
	gdImageDestroy(im);

	if (pattern)
		PG_RETURN_BYTEA_P(pattern);
	else
		PG_RETURN_NULL();
}

/*
 * Load pattern from png image in bytea.
 */
Datum
png2pattern(PG_FUNCTION_ARGS)
{
	bytea *img = PG_GETARG_BYTEA_P(0);
	Pattern *pattern;
	gdImagePtr im;

	im = gdImageCreateFromPngPtr(VARSIZE_ANY_EXHDR(img), VARDATA_ANY(img));
	PG_FREE_IF_COPY(img, 0);
	if (!im)
	{
		elog(NOTICE, "Error loading png");
		PG_RETURN_NULL();
	}
	pattern = image2pattern(im);
	gdImageDestroy(im);

	if (pattern)
		PG_RETURN_BYTEA_P(pattern);
	else
		PG_RETURN_NULL();
}

/*
 * Load pattern from png image in bytea.
 */
Datum
gif2pattern(PG_FUNCTION_ARGS)
{
	bytea *img = PG_GETARG_BYTEA_P(0);
	Pattern *pattern;
	gdImagePtr im;

	im = gdImageCreateFromGifPtr(VARSIZE_ANY_EXHDR(img), VARDATA_ANY(img));
	PG_FREE_IF_COPY(img, 0);
	if (!im)
	{
		elog(NOTICE, "Error loading gif");
		PG_RETURN_NULL();
	}
	pattern = image2pattern(im);
	gdImageDestroy(im);

	if (pattern)
		PG_RETURN_BYTEA_P(pattern);
	else
		PG_RETURN_NULL();
}

/*
 * Extract signature from pattern.
 */
Datum
pattern2signature(PG_FUNCTION_ARGS)
{
	bytea *patternData = PG_GETARG_BYTEA_P(0);
	PatternData *pattern = (PatternData *)VARDATA_ANY(patternData);
	Signature *signature = (Signature *)palloc(sizeof(Signature));

	calcSignature(pattern, signature);
	PG_FREE_IF_COPY(patternData, 0);

#ifdef DEBUG_INFO
	debugPrintSignature(signature, "/tmp/signature.raw");
#endif

	PG_RETURN_POINTER(signature);
}

/*
 * Shuffle pattern values in order to make further comparisons less sensitive
 * to shift. Shuffling is actually a build of "w" radius in rectangle
 * "(x, y) - (x + sX, y + sY)".
 */
static void
shuffle(PatternData *dst, PatternData *src, int x, int y, int sX, int sY, int w)
{
	int i, j;

	for (i = x; i < x + sX; i++)
	{
		for (j = y; j < y + sY; j++)
		{
			int ii, jj;
			int ii_min = Max(x, i - w),
				ii_max = Min(x + sX, i + w + 1),
				jj_min = Max(y, j - w),
				jj_max = Min(y + sY, j + w + 1);
			float sum = 0.0f, sum_r = 0.0f;

			for (ii = ii_min; ii < ii_max; ii++)
			{
				for (jj = jj_min; jj < jj_max; jj++)
				{
					float r = (i - ii) * (i - ii) + (j - jj) * (j - jj);
					r = 1.0f - sqrt(r) / (float)w;
					if (r <= 0.0f)
						continue;
					sum += src->values[ii][jj] * src->values[ii][jj] * r;
					sum_r += r;
				}
			}
			Assert (sum >= 0.0f);
			Assert (sum_r > 0.0f);
			dst->values[i][j] = sqrt(sum / sum_r);
		}
	}
}

/*
 * Shuffle pattern: call "shuffle" for each region of wavelet-transformed
 * pattern. For each region, blur radius is selected accordingly to its size;
 */
Datum
shuffle_pattern(PG_FUNCTION_ARGS)
{
	bytea *patternDataSrc = PG_GETARG_BYTEA_P(0);
	bytea *patternDataDst;
	PatternData *patternDst, *patternSrc;
	int size = PATTERN_SIZE;

	patternDataDst = (bytea *)palloc(VARSIZE_ANY(patternDataSrc));
	memcpy(patternDataDst, patternDataSrc, VARSIZE_ANY(patternDataSrc));
	patternSrc = (PatternData *)VARDATA_ANY(patternDataSrc);
	patternDst = (PatternData *)VARDATA_ANY(patternDataDst);

	while (size > 4)
	{
		size /= 2;
		shuffle(patternDst, patternSrc, size, 0, size, size, size / 4);
		shuffle(patternDst, patternSrc, 0, size, size, size, size / 4);
		shuffle(patternDst, patternSrc, size, size, size, size, size / 4);
	}
#ifdef DEBUG_INFO
	debugPrintPattern(patternDst, "/tmp/pattern4.raw", false);
#endif

	PG_FREE_IF_COPY(patternDataSrc, 0);

	PG_RETURN_POINTER(patternDataDst);
}

/*
 * Read float4 from string while skipping " ()," symbols.
 */
static float
read_float(char **s, char *type_name, char *orig_string)
{
	char	c,
		   *start;
	float	result;

	while (true)
	{
		c = **s;
		switch (c)
		{
			case ' ':
			case '(':
			case ')':
			case ',':
				(*s)++;
				continue;
			case '\0':
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						 errmsg("invalid input syntax for type %s: \"%s\"",
								type_name, orig_string)));
			default:
				break;
		}
		break;
	}

	start = *s;
	result = strtof(start, s);

	if (start == *s)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for type %s: \"%s\"",
						type_name, orig_string)));

	return result;
}

/*
 * Input "pattern" type from its textual representation.
 */
Datum
pattern_in(PG_FUNCTION_ARGS)
{
	char	   *source = PG_GETARG_CSTRING(0);
	Pattern	   *pattern = (Pattern *) palloc(sizeof(Pattern));
	char	   *s;
	int			i, j;

	SET_VARSIZE(pattern, sizeof(Pattern));
	s = source;
	for (i = 0; i < PATTERN_SIZE; i++)
		for (j = 0; j < PATTERN_SIZE; j++)
			pattern->data.values[i][j] = read_float(&s, "pattern", source);

	PG_RETURN_POINTER(pattern);
}

/*
 * Output for type "pattern": return textual representation of matrix.
 */
Datum
pattern_out(PG_FUNCTION_ARGS)
{
	bytea *patternData = PG_GETARG_BYTEA_P(0);
	PatternData *pattern = (PatternData *) VARDATA_ANY(patternData);
	StringInfoData buf;
	int i, j;

	initStringInfo(&buf);

	appendStringInfoChar(&buf, '(');
	for (i = 0; i < PATTERN_SIZE; i++)
	{
		if (i > 0)
			appendStringInfo(&buf, ", ");
		appendStringInfoChar(&buf, '(');
		for (j = 0; j < PATTERN_SIZE; j++)
		{
			if (j > 0)
				appendStringInfo(&buf, ", ");
			appendStringInfo(&buf, "%f", pattern->values[i][j]);
		}
		appendStringInfoChar(&buf, ')');
	}
	appendStringInfoChar(&buf, ')');

	PG_FREE_IF_COPY(patternData, 0);
	PG_RETURN_CSTRING(buf.data);
}

/*
 * Input "signature" type from its textual representation.
 */
Datum
signature_in(PG_FUNCTION_ARGS)
{
	char	   *source = PG_GETARG_CSTRING(0);
	Signature  *signature = (Signature *) palloc(sizeof(Signature));
	char	   *s;
	int			i;

	SET_VARSIZE(signature, sizeof(Signature));
	s = source;
	for (i = 0; i < SIGNATURE_SIZE; i++)
		signature->values[i] = read_float(&s, "signature", source);

	PG_RETURN_POINTER(signature);
}

/*
 * Output for type "signature": return textual representation of vector.
 */
Datum
signature_out(PG_FUNCTION_ARGS)
{
	Signature *signature = (Signature *)PG_GETARG_POINTER(0);
	StringInfoData buf;
	int i;

	initStringInfo(&buf);

	appendStringInfoChar(&buf, '(');
	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		if (i > 0)
			appendStringInfo(&buf, ", ");
		appendStringInfo(&buf, "%f", signature->values[i]);
	}
	appendStringInfoChar(&buf, ')');

	PG_FREE_IF_COPY(signature, 0);
	PG_RETURN_CSTRING(buf.data);
}

/*
 * Calculate summary of square difference between "patternA" and "patternB"
 * in rectangle "(x, y) - (x + sX, y + sY)".
 */
static float
calcDiff(PatternData *patternA, PatternData *patternB, int x, int y,
																int sX, int sY)
{
	int i, j;
	float summ = 0.0f, val;
	for (i = x; i < x + sX; i++)
	{
		for (j = y; j < y + sY; j++)
		{
			val =   patternA->values[i][j]
			      - patternB->values[i][j];
			summ += val * val;
		}
	}
	return summ;
}

/*
 * Distance between patterns is square root of the summary of difference between
 * regions of wavelet-transformed pattern corrected by their sized. Difference
 * of each region is summary of square difference between values.
 */
Datum
pattern_distance(PG_FUNCTION_ARGS)
{
	bytea *patternDataA = PG_GETARG_BYTEA_P(0);
	PatternData *patternA = (PatternData *)VARDATA_ANY(patternDataA);
	bytea *patternDataB = PG_GETARG_BYTEA_P(1);
	PatternData *patternB = (PatternData *)VARDATA_ANY(patternDataB);
	float distance = 0.0f, val;
	int size = PATTERN_SIZE;
	float mult = 1.0f;

	while (size > 1)
	{
		size /= 2;
		distance += mult * calcDiff(patternA, patternB, size, 0, size, size);
		distance += mult * calcDiff(patternA, patternB, 0, size, size, size);
		distance += mult * calcDiff(patternA, patternB, size, size, size, size);
		mult *= 2.0f;
	}
	val = patternA->values[0][0] - patternB->values[0][0];
	distance += mult * val * val;
	distance = sqrt(distance);

	PG_RETURN_FLOAT4(distance);
}

/*
 * Distance between signatures: mean-square difference between signatures.
 */
Datum
signature_distance(PG_FUNCTION_ARGS)
{
	Signature *signatureA = (Signature *)PG_GETARG_POINTER(0);
	Signature *signatureB = (Signature *)PG_GETARG_POINTER(1);
	float distance = 0.0f, val;
	int i;

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		val = signatureA->values[i] - signatureB->values[i];
		distance += val * val;
	}
	distance = sqrt(distance);

	PG_RETURN_FLOAT4(distance);
}

/*
 * Make pattern from gd image.
 */
static void
makePattern(gdImagePtr im, PatternData *pattern)
{
	int i, j;
	for (i = 0; i < PATTERN_SIZE; i++)
		for (j = 0; j < PATTERN_SIZE; j++)
		{
			int pixel = gdImageGetTrueColorPixel(im, i, j);
			float red = (float) gdTrueColorGetRed(pixel) / 255.0,
				  green = (float) gdTrueColorGetGreen(pixel) / 255.0,
				  blue = (float) gdTrueColorGetBlue(pixel) / 255.0;
			pattern->values[i][j] = sqrt((red * red + green * green + blue * blue) / 3.0f);
		}
}

/*
 * Normalize pattern: make it minimal value equal to 0 and
 * maximum value equal to 1.
 */
static void
normalizePattern(PatternData *pattern)
{
	float min = 1.0f, max = 0.0f, val;
	int i, j;
	for (i = 0; i < PATTERN_SIZE; i++)
	{
		for (j = 0; j < PATTERN_SIZE; j++)
		{
			val = pattern->values[i][j];
			if (val < min) min = val;
			if (val > max) max = val;

		}
	}
	for (i = 0; i < PATTERN_SIZE; i++)
	{
		for (j = 0; j < PATTERN_SIZE; j++)
		{
			pattern->values[i][j] = (pattern->values[i][j] - min) / (max - min);
		}
	}
}

/*
 * Do Haar wavelet transform over pattern.
 */
static void
waveletTransform(PatternData *dst, PatternData *src, int size)
{
	if (size > 1)
	{
		int i, j;
		size /= 2;
		for (i = 0; i < size; i++)
		{
			for (j = 0; j < size; j++)
			{
				dst->values[i + size][j] =        ( - src->values[2 * i][2 * j]     + src->values[2 * i + 1][2 * j]
				                                    - src->values[2 * i][2 * j + 1] + src->values[2 * i + 1][2 * j + 1]) / 4.0f;
				dst->values[i][j + size] =        ( - src->values[2 * i][2 * j]     - src->values[2 * i + 1][2 * j]
				                                    + src->values[2 * i][2 * j + 1] + src->values[2 * i + 1][2 * j + 1]) / 4.0f;
				dst->values[i + size][j + size] = (   src->values[2 * i][2 * j]     - src->values[2 * i + 1][2 * j]
				                                    - src->values[2 * i][2 * j + 1] + src->values[2 * i + 1][2 * j + 1]) / 4.0f;
			}
		}
		for (i = 0; i < size; i++)
		{
			for (j = 0; j < size; j++)
			{
				src->values[i][j] =               (   src->values[2 * i][2 * j]     + src->values[2 * i + 1][2 * j]
				                                    + src->values[2 * i][2 * j + 1] + src->values[2 * i + 1][2 * j + 1]) / 4.0f;
			}
		}
		waveletTransform(dst, src, size);
	}
	else
	{
		dst->values[0][0] = src->values[0][0];
	}
}

/*
 * Calculate summary of squares in rectangle "(x, y) - (x + sX, y + sY)".
 */
static float
calcSumm(PatternData *pattern, int x, int y, int sX, int sY)
{
	int i, j;
	float summ = 0.0f, val;
	for (i = x; i < x + sX; i++)
	{
		for (j = y; j < y + sY; j++)
		{
			val = pattern->values[i][j];
			summ += val * val;
		}
	}
	return sqrt(summ);
}

/*
 * Make short signature from pattern.
 */
static void
calcSignature(PatternData *pattern, Signature *signature)
{
	int size = PATTERN_SIZE / 2;
	int i = 0;
	float mult = 1.0f;

	while (size > 1)
	{
		size /= 2;
		signature->values[i++] = mult * calcSumm(pattern, size, 0, size, size);
		signature->values[i++] = mult * calcSumm(pattern, 0, size, size, size);
		signature->values[i++] = mult * calcSumm(pattern, size, size, size, size);
		mult *= 2.0f;
	}
	signature->values[SIGNATURE_SIZE - 1] = pattern->values[0][0];
}

#ifdef DEBUG_INFO

static void
debugPrintPattern(PatternData *pattern, const char *filename, bool color)
{
	int i, j;
	FILE *out = fopen(filename, "wb");
	for (j = 0; j < PATTERN_SIZE; j++)
	{
		for (i = 0; i < PATTERN_SIZE; i++)
		{
			float val = pattern->values[i][j];
			if (!color)
			{
				fputc((int)(val * 255.999f), out);
				fputc((int)(val * 255.999f), out);
				fputc((int)(val * 255.999f), out);
			}
			else
			{
				if (val >= 0.0f)
				{
					fputc(0, out);
					fputc((int)(val * 255.999f), out);
					fputc(0, out);
				}
				else
				{
					fputc((int)(- val * 255.999f), out);
					fputc(0, out);
					fputc(0, out);
				}
			}
		}
	}
	fclose(out);
}

static void
debugPrintSignature(Signature *signature, const char *filename)
{
	int i;
	float max = 0.0f;
	FILE *out = fopen(filename, "wb");

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		max = Max(max, signature->values[i]);
	}

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		float val = signature->values[i];
		val = val / max;
		fputc((int)(val * 255.999f), out);
		fputc((int)(val * 255.999f), out);
		fputc((int)(val * 255.999f), out);
	}
	fclose(out);
}
#endif
