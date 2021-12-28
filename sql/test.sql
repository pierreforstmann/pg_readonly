--
-- test.sql
--
create extension pg_readonly;
--
select get_cluster_readonly();
select unset_cluster_readonly();
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
