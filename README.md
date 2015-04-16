ImgSmlr – similar images search for PostgreSQL
==============================================

Introduction
------------

ImgSmlr – is an PostgreSQL which implements similar images searching
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
[github](https://github.com/akorotkov/imgsmlr)
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
    
    $ git clone https://github.com/akorotkov/imgsmlr.git
    $ cd imgsmlr
    $ make USE_PGXS=1
    $ sudo make USE_PGXS=1 install
    $ make USE_PGXS=1 installcheck
    $ psql DB -c "CREATE EXTENSION imgsmlr;"

Usage
-----

| Type      | Storage length |                              Description                           |
| --------- |--------------: | ------------------------------------------------------------------ |
| pattern   | 16388 bytes    | Result of Haar wavelet transform on the image                      |
| signature | 64 bytes       | Short representation of pattern for fast search using GiST indexes |

|          Function          | Return type |                      Description                    |
| -------------------------- |-------------| --------------------------------------------------- |
| jpeg2pattern(bytea)        | pattern     | Convert jpeg image into pattern                     |
| png2pattern(bytea)         | pattern     | Convert png image into pattern                      |
| gif2pattern(bytea)         | pattern     | Convert gif image into pattern                      |
| pattern2signature(pattern) | signature   | Create signature from pattern                       |
| shuffle_pattern(pattern)   | pattern     | Shuffle pattern for less sensibility to image shift |

| Operator | Left type | Right type | Return type |                Description                |
| -------- |-----------| ---------- | ----------- | ----------------------------------------- |
| <->      | pattern   | pattern    | float8      | Eucledian distance between two patterns   |
| <->      | signature | signature  | float8      | Eucledian distance between two signatures |

Example
-------

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
CREATE INDEX pat_signature_idx ON pat USING gist (signature);
CREATE INDEX pat_id_idx ON pat(id);
```

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
