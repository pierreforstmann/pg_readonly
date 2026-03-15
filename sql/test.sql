--
-- test.sql
--
create extension pg_readonly;
--
select unset_cluster_readonly();
select get_cluster_readonly();
--
drop table t;
drop function f;
--
create table t(i int);

create function f(IN p int, OUT i int) returns int as $$
begin
    insert into t(i) values (f.p) returning t.i into f.i;
end$$
language plpgsql;

select set_cluster_readonly();

select * from t;

insert into t values (1);

select f(1);
-- However, in this same session, if you call f() before entering in read only mode, 
-- it is then able to write during read only mode:

select unset_cluster_readonly();

select f(1);

select set_cluster_readonly();

insert into t values (2);

select f(2);

select get_cluster_readonly();

select f(3);

select * from t;

with inserted as (insert into t values(3) returning 3) select * from inserted;
select * from t;

-- DDL is also blocked via ProcessUtility hook
CREATE TABLE t2(i int);

-- DO block: INSERT inside a DO block should be blocked by the executor hook
DO $$ BEGIN INSERT INTO t VALUES (99); END $$;
select * from t;

-- Large object creation: lo_create() is a SELECT-callable function that
-- writes to the pg_largeobject catalog internally.
SELECT lo_create(0);
