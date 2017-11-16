CREATE EXTENSION imgsmlr;

CREATE TABLE image (id integer PRIMARY KEY, data bytea);
CREATE TABLE tmp (data text);

\copy tmp from 'data/1.jpg.hex'
INSERT INTO image VALUES (1, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/2.png.hex'
INSERT INTO image VALUES (2, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/3.gif.hex'
INSERT INTO image VALUES (3, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/4.jpg.hex'
INSERT INTO image VALUES (4, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/5.png.hex'
INSERT INTO image VALUES (5, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/6.gif.hex'
INSERT INTO image VALUES (6, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/7.jpg.hex'
INSERT INTO image VALUES (7, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/8.png.hex'
INSERT INTO image VALUES (8, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/9.gif.hex'
INSERT INTO image VALUES (9, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/10.jpg.hex'
INSERT INTO image VALUES (10, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/11.png.hex'
INSERT INTO image VALUES (11, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;
\copy tmp from 'data/12.gif.hex'
INSERT INTO image VALUES (12, (SELECT decode(string_agg(data, ''), 'hex') FROM tmp));
TRUNCATE tmp;

CREATE TABLE pat AS (
    SELECT
        id,
        shuffle_pattern(pattern)::text::pattern AS pattern,
        pattern2signature(pattern)::text::signature AS signature
    FROM (
        SELECT 
            id,
            (CASE WHEN id % 3 = 1 THEN jpeg2pattern(data)
                  WHEN id % 3 = 2 THEN png2pattern(data)
                  WHEN id % 3 = 0 THEN gif2pattern(data)
                  ELSE NULL END) AS pattern 
        FROM 
            image
    ) x 
);

ALTER TABLE pat ADD PRIMARY KEY (id);
CREATE INDEX pat_signature_idx ON pat USING gist (signature);

SELECT p1.id, p2.id, round((p1.pattern <-> p2.pattern)::numeric, 4) FROM pat p1, pat p2 ORDER BY p1.id, p2.id;
SELECT p1.id, p2.id, round((p1.signature <-> p2.signature)::numeric, 4) FROM pat p1, pat p2 ORDER BY p1.id, p2.id;

SET enable_seqscan = OFF;

SELECT id FROM pat ORDER BY signature <-> (SELECT signature FROM pat WHERE id = 1) LIMIT 3;
SELECT id FROM pat ORDER BY signature <-> (SELECT signature FROM pat WHERE id = 4) LIMIT 3;
SELECT id FROM pat ORDER BY signature <-> (SELECT signature FROM pat WHERE id = 7) LIMIT 3;
SELECT id FROM pat ORDER BY signature <-> (SELECT signature FROM pat WHERE id = 10) LIMIT 3;
