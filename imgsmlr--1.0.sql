/* imgsmlr/imgsmlr--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION imgsmlr" to load this file. \quit

--
--  PostgreSQL code for IMGSMLR.
--

CREATE FUNCTION pattern_in(cstring)
RETURNS pattern
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pattern_out(pattern)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE pattern (
	INTERNALLENGTH = -1,
	INPUT = pattern_in,
	OUTPUT = pattern_out,
	STORAGE = extended
);

CREATE FUNCTION signature_in(cstring)
RETURNS signature
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_out(signature)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE signature (
	INTERNALLENGTH = 64,
	INPUT = signature_in,
	OUTPUT = signature_out,
	ALIGNMENT = float
);

CREATE FUNCTION jpeg2pattern(bytea)
RETURNS pattern
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION png2pattern(bytea)
RETURNS pattern
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION gif2pattern(bytea)
RETURNS pattern
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pattern2signature(pattern)
RETURNS signature
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pattern_distance(pattern, pattern)
RETURNS float4
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_distance(signature, signature)
RETURNS float4
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR <-> (
	LEFTARG = pattern,
	RIGHTARG = pattern,
	PROCEDURE = pattern_distance
);

CREATE OPERATOR <-> (
	LEFTARG = signature,
	RIGHTARG = signature,
	PROCEDURE = signature_distance
);

CREATE FUNCTION shuffle_pattern(pattern)
RETURNS pattern
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_consistent(internal,signature,int,oid,internal)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_compress(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_decompress(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_penalty(internal,internal,internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_picksplit(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_union(internal, internal)
RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_same(bytea, bytea, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION signature_gist_distance(internal, text, int, oid)
RETURNS float8
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS gist_signature_ops
    DEFAULT FOR TYPE signature USING gist AS
	OPERATOR    1   <-> FOR ORDER BY pg_catalog.float_ops,
	FUNCTION	1	signature_consistent (internal, signature, int, oid, internal),
	FUNCTION	2	signature_union (internal, internal),
	FUNCTION	3	signature_compress (internal),
	FUNCTION	4	signature_decompress (internal),
	FUNCTION	5	signature_penalty (internal, internal, internal),
	FUNCTION	6	signature_picksplit (internal, internal),
	FUNCTION	7	signature_same (bytea, bytea, internal),
	FUNCTION	8	signature_gist_distance (internal, text, int, oid),
	STORAGE		bytea;
