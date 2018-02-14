[![Build Status](https://travis-ci.org/postgrespro/imgsmlr.svg?branch=master)](https://travis-ci.org/postgrespro/imgsmlr)
[![codecov](https://codecov.io/gh/postgrespro/imgsmlr/branch/master/graph/badge.svg)](https://codecov.io/gh/postgrespro/imgsmlr)
[![GitHub license](https://img.shields.io/badge/license-PostgreSQL-blue.svg)](https://raw.githubusercontent.com/postgrespro/imgsmlr/master/LICENSE)

ImgSmlr – similar images search for PostgreSQL
==============================================

Introduction
------------

ImgSmlr – is a PostgreSQL extension which implements similar images searching
functionality.

ImgSmlr method is based on Haar wavelet transform. The goal of ImgSmlr is not
to provide most advanced state of art similar images searching methods. ImgSmlr
was written as sample extension which illustrate how PostgreSQL extendability
could cover such untypical tasks for RDBMS as similar images search.

Authors
-------

 * Alexander Korotkov <aekorotkov@gmail.com>, Postgres Professional, Moscow, Russia

Availability
------------

ImgSmlr is released as an extension and not available in default PostgreSQL
installation. It is available from
[github](https://github.com/postgrespro/imgsmlr)
under the same license as
[PostgreSQL](http://www.postgresql.org/about/licence/)
and supports PostgreSQL 9.1+.

Installation
------------

Before build and install ImgSmlr you should ensure following:
    
 * PostgreSQL version is 9.1 or higher.
 * You have development package of PostgreSQL installed or you built
   PostgreSQL from source.
 * You have gd2 library installed on your system.
 * Your PATH variable is configured so that pg\_config command available.
    
Typical installation procedure may look like this:
    
    $ git clone https://github.com/postgrespro/imgsmlr.git
    $ cd imgsmlr
    $ make USE_PGXS=1
    $ sudo make USE_PGXS=1 install
    $ make USE_PGXS=1 installcheck
    $ psql DB -c "CREATE EXTENSION imgsmlr;"

Usage
-----

ImgSmlr offers two datatypes: pattern and signature.

| Datatype  | Storage length |                              Description                           |
| --------- |--------------: | ------------------------------------------------------------------ |
| pattern   | 16388 bytes    | Result of Haar wavelet transform on the image                      |
| signature | 64 bytes       | Short representation of pattern for fast search using GiST indexes |

There is set of functions *2pattern(bytea) which converts bynary data in given format into pattern. Convertion into pattern consists of following steps.

 * Decompress image.
 * Make image black&white.
 * Resize image to 64x64 pixels.
 * Apply Haar wavelet transform to the image.

Pattern could be converted into signature and shuffled for less sensitivity to image shift.

|          Function          | Return type |                      Description                    |
| -------------------------- |-------------| --------------------------------------------------- |
| jpeg2pattern(bytea)        | pattern     | Convert jpeg image into pattern                     |
| png2pattern(bytea)         | pattern     | Convert png image into pattern                      |
| gif2pattern(bytea)         | pattern     | Convert gif image into pattern                      |
| pattern2signature(pattern) | signature   | Create signature from pattern                       |
| shuffle_pattern(pattern)   | pattern     | Shuffle pattern for less sensitivity to image shift |

Both pattern and signature datatypes supports `<->` operator for eucledian distance. Signature also supports GiST indexing with KNN on `<->` operator.

| Operator | Left type | Right type | Return type |                Description                |
| -------- |-----------| ---------- | ----------- | ----------------------------------------- |
| <->      | pattern   | pattern    | float8      | Eucledian distance between two patterns   |
| <->      | signature | signature  | float8      | Eucledian distance between two signatures |

The idea is to find top N similar images by signature using GiST index. Then find top n (n < N) similar images by pattern from top N similar images by signature.

Example
-------

Let us assume we have an `image` table with columns `id` and `data` where `data` column contains binary jpeg data. We can create `pat` table with patterns and signatures of given images using following query.

```sql
CREATE TABLE pat AS (
	SELECT
		id,
		shuffle_pattern(pattern) AS pattern, 
		pattern2signature(pattern) AS signature 
	FROM (
		SELECT 
			id, 
			jpeg2pattern(data) AS pattern 
		FROM 
			image
	) x 
);
```

Then let's create primary key for `pat` table and GiST index for signatures.

```sql
ALTER TABLE pat ADD PRIMARY KEY (id);
CREATE INDEX pat_signature_idx ON pat USING gist (signature);
```

Prelimimary work is done. Now we can search for top 10  similar images to given image with specified id using following query.

```sql
SELECT
	id,
	smlr
FROM
(
	SELECT
		id,
		pattern <-> (SELECT pattern FROM pat WHERE id = :id) AS smlr
	FROM pat
	WHERE id <> :id
	ORDER BY
		signature <-> (SELECT signature FROM pat WHERE id = :id)
	LIMIT 100
) x
ORDER BY x.smlr ASC 
LIMIT 10
```

Inner query selects top 100 images by signature using GiST index. Outer query search for top 10 images by pattern from images found by inner query. You can adjust both of number to achieve better search results on your images collection.
