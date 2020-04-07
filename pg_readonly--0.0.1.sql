-- \echo Use "CREATE EXTENSION pg_readonly" to load this file . \quit
DROP FUNCTION IF EXISTS set_cluster_readonly();
DROP FUNCTION IF EXISTS unset_cluster_readonly();
DROP FUNCTION IF EXISTS get_cluster_readonly();
--
CREATE FUNCTION set_cluster_readonly() RETURNS bool
 AS 'pg_readonly.so', 'pgro_set_readonly'
 LANGUAGE C STRICT;
--
CREATE FUNCTION unset_cluster_readonly() RETURNS bool
 AS 'pg_readonly.so', 'pgro_unset_readonly'
 LANGUAGE C STRICT;
--
CREATE FUNCTION get_cluster_readonly() RETURNS bool
 AS 'pg_readonly.so', 'pgro_get_readonly'
 LANGUAGE C STRICT;
--

