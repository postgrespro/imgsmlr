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
 *    imgsmlr/imgsmlr_idx.c
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "imgsmlr.h"
#include "access/gist.h"
#include "access/gist_private.h"
#include "access/skey.h"
#include "c.h"
#include <gd.h>
#include <stdio.h>
#include <math.h>


PG_FUNCTION_INFO_V1(signature_consistent);
PG_FUNCTION_INFO_V1(signature_compress);
PG_FUNCTION_INFO_V1(signature_decompress);
PG_FUNCTION_INFO_V1(signature_penalty);
PG_FUNCTION_INFO_V1(signature_picksplit);
PG_FUNCTION_INFO_V1(signature_union);
PG_FUNCTION_INFO_V1(signature_same);
PG_FUNCTION_INFO_V1(signature_gist_distance);

Datum		signature_consistent(PG_FUNCTION_ARGS);
Datum		signature_compress(PG_FUNCTION_ARGS);
Datum		signature_decompress(PG_FUNCTION_ARGS);
Datum		signature_penalty(PG_FUNCTION_ARGS);
Datum		signature_picksplit(PG_FUNCTION_ARGS);
Datum		signature_union(PG_FUNCTION_ARGS);
Datum		signature_same(PG_FUNCTION_ARGS);
Datum		signature_gist_distance(PG_FUNCTION_ARGS);

static void set_signature(Signature  *dst, bytea *src);
static void extend_signature(Signature  *dst, bytea *srcBytea);
static void union_intersect_size(bytea  *dstBytea, bytea *srcBytea, float *unionSize, float *intersectSize);
static float key_size(bytea *key);

Datum
signature_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	bytea *res;

	if (entry->leafkey)
	{
		GISTENTRY  *retval;

		res = (bytea *)palloc(sizeof(Signature) + VARHDRSZ);
		SET_VARSIZE(res, sizeof(Signature) + VARHDRSZ);
		memcpy(VARDATA(res), DatumGetPointer(entry->key), sizeof(Signature));

		retval = (GISTENTRY *) palloc(sizeof(GISTENTRY));
		gistentryinit(*retval, PointerGetDatum(res),
					  entry->rel, entry->page,
					  entry->offset, FALSE);

		PG_RETURN_POINTER(retval);
	}
	else
	{
		PG_RETURN_POINTER(entry);
	}
}

Datum
signature_decompress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	bytea	   *key = DatumGetByteaP(PG_DETOAST_DATUM(entry->key));

	if (key != DatumGetByteaP(entry->key))
	{
		GISTENTRY  *retval = (GISTENTRY *) palloc(sizeof(GISTENTRY));

		gistentryinit(*retval, PointerGetDatum(key),
					  entry->rel, entry->page,
					  entry->offset, FALSE);
		PG_RETURN_POINTER(retval);
	}
	PG_RETURN_POINTER(entry);
}

Datum
signature_consistent(PG_FUNCTION_ARGS)
{
	bool	   *recheck = (bool *) PG_GETARG_POINTER(4);
	*recheck = true;

	PG_RETURN_BOOL(true);
}

static void
set_signature(Signature  *dst, bytea *src)
{
	CHECK_SIGNATURE_KEY(src);

	memcpy(dst, VARDATA_ANY(src), sizeof(Signature));
	if (VARSIZE_ANY_EXHDR(src) == sizeof(Signature))
		memcpy(dst + 1, VARDATA_ANY(src), sizeof(Signature));
	else
		memcpy(dst + 1, VARDATA_ANY(src) + sizeof(Signature), sizeof(Signature));
}

static void
extend_signature(Signature  *dst, bytea *srcBytea)
{
	Signature *src = (Signature *)VARDATA_ANY(srcBytea);
	int i;

	CHECK_SIGNATURE_KEY(srcBytea);

	for (i = 0; i < SIGNATURE_SIZE; i++)
		dst->values[i] = Min(dst->values[i], src->values[i]);

	dst++;
	if (VARSIZE_ANY_EXHDR(srcBytea) == 2 * sizeof(Signature))
		src++;

	for (i = 0; i < SIGNATURE_SIZE; i++)
		dst->values[i] = Max(dst->values[i], src->values[i]);
}

Datum
signature_union(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	int		   *sizep = (int *) PG_GETARG_POINTER(1);
	int			i;
	bytea	   *out;
	Signature  *outSignature;

	out = (bytea *)palloc(2 * sizeof(Signature) + VARHDRSZ);
	SET_VARSIZE(out, 2 * sizeof(Signature) + VARHDRSZ);
	outSignature = (Signature  *)VARDATA(out);
	set_signature(outSignature, DatumGetByteaP(entryvec->vector[0].key));

	for (i = 1; i < entryvec->n; i++)
	{
		extend_signature(outSignature, DatumGetByteaP(entryvec->vector[i].key));
	}

	*sizep = VARSIZE(out);
	PG_RETURN_POINTER(out);
}

Datum
signature_same(PG_FUNCTION_ARGS)
{
	bytea	   *b1 = PG_GETARG_BYTEA_P(0);
	bytea	   *b2 = PG_GETARG_BYTEA_P(1);
	bool	   *result = (bool *) PG_GETARG_POINTER(2);

	Assert(VARSIZE_ANY_EXHDR(b1) == sizeof(Signature) ||
		   VARSIZE_ANY_EXHDR(b1) == 2 * sizeof(Signature));
	Assert(VARSIZE_ANY_EXHDR(b2) == sizeof(Signature) ||
		   VARSIZE_ANY_EXHDR(b2) == 2 * sizeof(Signature));

	if (VARSIZE_ANY_EXHDR(b1) == VARSIZE_ANY_EXHDR(b2))
	{
		*result = (memcpy(b1, b2, VARSIZE_ANY_EXHDR(b1)) == 0);
	}
	else
	{
		*result = false;
	}

	PG_RETURN_POINTER(result);
}

Datum
signature_penalty(PG_FUNCTION_ARGS)
{
	GISTENTRY  *origentry = (GISTENTRY *) PG_GETARG_POINTER(0);
	GISTENTRY  *newentry = (GISTENTRY *) PG_GETARG_POINTER(1);
	float	   *result = (float *) PG_GETARG_POINTER(2);
	float		intersect_size, union_size;

	union_intersect_size(
		DatumGetByteaP(origentry->key),
		DatumGetByteaP(newentry->key),
		&union_size,
		&intersect_size);

	*result = union_size - key_size(DatumGetByteaP(origentry->key));

	PG_RETURN_FLOAT8(*result);
}

static void
union_intersect_size(bytea  *dstBytea, bytea *srcBytea, float *unionSize, float *intersectSize)
{
	Signature *dstMin = (Signature *)VARDATA_ANY(dstBytea), *dstMax;
	Signature *srcMin = (Signature *)VARDATA_ANY(srcBytea), *srcMax;
	float unionSizeAccum = 1.0f, intersectSizeAccum = 1.0f;
	int i;

	CHECK_SIGNATURE_KEY(dstBytea);
	CHECK_SIGNATURE_KEY(srcBytea);

	srcMax = srcMin;
	if (VARSIZE_ANY_EXHDR(srcBytea) == 2 * sizeof(Signature))
		srcMax++;

	dstMax = dstMin;
	if (VARSIZE_ANY_EXHDR(dstBytea) == 2 * sizeof(Signature))
		dstMax++;

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		float unionRange = Max(dstMax->values[i], srcMax->values[i]) -
						   Min(dstMin->values[i], srcMin->values[i]);
		float intersectRange = Min(dstMax->values[i], srcMax->values[i]) -
							   Max(dstMin->values[i], srcMin->values[i]);
		unionSizeAccum *= unionRange;
		if (intersectRange < 0.0f)
			intersectRange = 0.0f;
		intersectSizeAccum *= intersectRange;
	}

	*unionSize = unionSizeAccum;
	*intersectSize = intersectSizeAccum;
}

static float
key_size(bytea *key)
{
	Signature *keyMin = (Signature *)VARDATA_ANY(key), *keyMax;
	float size = 1.0f;
	int i;

	CHECK_SIGNATURE_KEY(key);

	keyMax = keyMin;
	if (VARSIZE_ANY_EXHDR(key) == 2 * sizeof(Signature))
		keyMax++;

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		float range = keyMax->values[i] - keyMin->values[i];
		size *= range;
	}
	return size;
}

Datum
signature_picksplit(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	GIST_SPLITVEC *v = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
	OffsetNumber i,
				j;
	bytea	   *datum_alpha,
			   *datum_beta;
	bytea	   *datum_l,
			   *datum_r;
	bool		firsttime;
	float		size_waste,
				waste;
	float		size_l,
				size_r;
	int			nbytes;
	OffsetNumber seed_1 = 1,
				seed_2 = 2;
	OffsetNumber *left,
			   *right;
	OffsetNumber maxoff;
	Signature *signature_l, *signature_r;
	bool *distributed;
	int undistributed_count;

	/*
	 * fprintf(stderr, "picksplit\n");
	 */
	maxoff = entryvec->n - 1;
	nbytes = (maxoff + 1) * sizeof(OffsetNumber);
	distributed = (bool *)palloc0(sizeof(bool) * (maxoff + 1));
	v->spl_left = (OffsetNumber *) palloc(nbytes);
	v->spl_right = (OffsetNumber *) palloc(nbytes);

	firsttime = true;
	waste = 0.0;

	for (i = FirstOffsetNumber; i < maxoff; i = OffsetNumberNext(i))
	{
		datum_alpha = DatumGetByteaP(entryvec->vector[i].key);
		for (j = OffsetNumberNext(i); j <= maxoff; j = OffsetNumberNext(j))
		{
			float unionSize, intersectSize;

			datum_beta = DatumGetByteaP(entryvec->vector[j].key);

			union_intersect_size(datum_alpha, datum_beta,
													&unionSize, &intersectSize);

			size_waste = unionSize - intersectSize;

			/*
			 * are these a more promising split than what we've already seen?
			 */

			if (size_waste > waste || firsttime)
			{
				waste = size_waste;
				seed_1 = i;
				seed_2 = j;
				firsttime = false;
			}
		}
	}

	left = v->spl_left;
	v->spl_nleft = 0;
	right = v->spl_right;
	v->spl_nright = 0;

	datum_l = (bytea *)palloc(2 * sizeof(Signature) + VARHDRSZ);
	SET_VARSIZE(datum_l, 2 * sizeof(Signature) + VARHDRSZ);
	signature_l = (Signature  *)VARDATA(datum_l);
	set_signature(signature_l, DatumGetByteaP(entryvec->vector[seed_1].key));
	size_l = key_size(datum_l);

	datum_r = (bytea *)palloc(2 * sizeof(Signature) + VARHDRSZ);
	SET_VARSIZE(datum_r, 2 * sizeof(Signature) + VARHDRSZ);
	signature_r = (Signature  *)VARDATA(datum_r);
	set_signature(signature_r, DatumGetByteaP(entryvec->vector[seed_2].key));
	size_r = key_size(datum_r);

	distributed[seed_1] = true;
	*left++ = seed_1;
	v->spl_nleft++;

	distributed[seed_2] = true;
	*right++ = seed_2;
	v->spl_nright++;
	undistributed_count = maxoff - 2;

	/*
	 * Now split up the regions between the two seeds.	An important property
	 * of this split algorithm is that the split vector v has the indices of
	 * items to be split in order in its left and right vectors.  We exploit
	 * this property by doing a merge in the code that actually splits the
	 * page.
	 *
	 * For efficiency, we also place the new index tuple in this loop. This is
	 * handled at the very end, when we have placed all the existing tuples
	 * and i == maxoff + 1.
	 */

	while (undistributed_count > 0)
	{
		OffsetNumber selected = InvalidOffsetNumber;
		float max_delta = 0.0;

		firsttime = true;

		for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
		{
			float union_l, union_r, intersect_l, intersect_r, delta;
			bool direction = false;

			if (distributed[i])
				continue;

			/*memcpy(signature_tmp, signature_l, sizeof(Signature) * 2);
			extend_signature(signature_tmp, DatumGetByteaP(entryvec->vector[i].key));
			union_intersect_size(tmp, datum_r, &union_l, &intersect_l);

			memcpy(signature_tmp, signature_r, sizeof(Signature) * 2);
			extend_signature(signature_tmp, DatumGetByteaP(entryvec->vector[i].key));
			union_intersect_size(tmp, datum_l, &union_r, &intersect_r);

			delta = intersect_l - intersect_r;*/

			union_intersect_size(datum_l, DatumGetByteaP(entryvec->vector[i].key), &union_l, &intersect_l);
			union_intersect_size(datum_r, DatumGetByteaP(entryvec->vector[i].key), &union_r, &intersect_r);

			delta = union_l - size_l - union_r + size_r;

			if (v->spl_nleft < v->spl_nright && delta < 0)
				direction = true;
			if (v->spl_nleft > v->spl_nright && delta > 0)
				direction = true;

			if (fabs(delta) > fabs(max_delta) ||
				(fabs(delta) == fabs(max_delta) && direction) ||
				firsttime)
			{
				max_delta = delta;
				selected = i;
				firsttime = false;
			}
		}
		Assert(OffsetNumberIsValid(selected));

		/* pick which page to add it to */
		if (max_delta < 0 || (max_delta == 0 && v->spl_nleft < v->spl_nright))
		{
			extend_signature(signature_l, DatumGetByteaP(entryvec->vector[selected].key));
			size_l = key_size(datum_l);
			*left++ = selected;
			v->spl_nleft++;
		}
		else
		{
			extend_signature(signature_r, DatumGetByteaP(entryvec->vector[selected].key));
			size_r = key_size(datum_r);
			*right++ = selected;
			v->spl_nright++;
		}
		distributed[selected] = true;
		undistributed_count--;
	}
	*left = *right = FirstOffsetNumber; /* sentinel value, see dosplit() */

	v->spl_ldatum = PointerGetDatum(datum_l);
	v->spl_rdatum = PointerGetDatum(datum_r);

	PG_RETURN_POINTER(v);
}

Datum
signature_gist_distance(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	/* StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);*/
	Signature *arg = (Signature *)PG_GETARG_POINTER(1), *keyMin, *keyMax;
	bytea *key = DatumGetByteaP(entry->key);
	double		distance = 0.0;
	int i = 0;

	CHECK_SIGNATURE_KEY(key);

	keyMin = (Signature *)VARDATA_ANY(key);
	keyMax = keyMin;
	if (VARSIZE_ANY_EXHDR(key) == 2 * sizeof(Signature))
		keyMax++;

	for (i = 0; i < SIGNATURE_SIZE; i++)
	{
		if (arg->values[i] < keyMin->values[i])
			distance += (arg->values[i] - keyMin->values[i]) * (arg->values[i] - keyMin->values[i]);
		if (arg->values[i] > keyMax->values[i])
			distance += (arg->values[i] - keyMax->values[i]) * (arg->values[i] - keyMax->values[i]);
	}

	PG_RETURN_FLOAT8(sqrt(distance));
}
