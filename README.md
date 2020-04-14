# pgreadonly_
pg_readonly is a PostgreSQL extension which allows to set all cluster databases read only.


# Installation
## Compiling

This module can be built using the standard PGXS infrastructure. For this to work, the pg_config program must be available in your $PATH:
  
`git clone https://github.com/pierreforstmann/pg_readonly.git` <br>
`cd pg_readonly` <br>
`make` <br>
`make install` <br>

## PostgreSQL setup

Extension can be loaded:

At server level with `shared_preload_libraries` parameter: <br> 
`shared_preload_libraries = 'pg_readonly'` <br>


## Usage
pg_readonly has no specific GUC. <br>
The read-only status is managed only in (shared) memory with a global flag. SQL functions are provided to set the flag, to unset the flag and to query the flag.
The current version of the extension does not allow to store the read-only status in a permanent way.<br>
The flag is at cluster level: either all databases are read-only or all database are read-write (the usual setting).<br>
The read-only mode is implemented by filtering SQL statements: SELECT statements are allowed but INSERT, UPDATE, DELETE and DDL statements are not allowed. 
This means that the databases are in read-only mode at SQL level: however, the checkpointer, background writer, walwriter and the autovacuum launcher are still running; this means that the database files are not read-only and that in some cases the database may still write to disk.<br>


## Example

To query the cluster status, call the function get_cluster_readonly which returns true is the cluster is read-only and false if not: <br>

`# select get_cluster_readonly();`
` get_cluster_readonly `
`----------------------`
` f`
`(1 row)`

To set the cluster read-only, call the function set_cluster_readonly:
`# select * from t;`
` x  |  y  `
`----+-----`
` 32 | abc`
`(1 row)`

The cluster is now read-only and only SELECT statements are allowed:

`pierre=# select * from t;`
` x  |  y  `
`----+-----`
` 32 | abc`
`(1 row)`

`# update t set x=33 where y='abc';`
`ERROR:  pg_readonly: invalid statement because cluster is read-only`
`# select 1 into tmp;`
`ERROR:  pg_readonly: invalid statement because cluster is read-only`
`# create table tmp(c text);`
`ERROR:  pg_readonly: invalid statement because cluster is read-only`

To set the cluster on read-write, call the function unset_cluster_readonly:

`# select unset_cluster_readonly();`
` unset_cluster_readonly `
`------------------------`
` t`
`(1 row)`

The cluster is now read-write and any DML or DDL statement is allowed:
`# update t set x=33 where y='abc';`
`UPDATE 1`
`# select * from t;`
` x  |  y  `
`----+-----`
` 33 | abc`
`(1 row)`

Note that any open transaction is cancelled by set_cluster_readonly function.<br>
The client is disconnected and gets the following message: <br>
`FATAL:  terminating connection due to conflict with recovery`
`DETAIL:  User query might have needed to see row versions that must be removed.`
`HINT:  In a moment you should be able to reconnect to the database and repeat your command.`
In PostgreSQL log, following messages are written:

`2020-04-14 16:00:14.531 CEST [29578] STATEMENT:  select set_cluster_readonly();`
`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: killing all transactions ...`
`2020-04-14 16:00:14.531 CEST [29578] STATEMENT:  select set_cluster_readonly();`
`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: PID 29569 signalled`
`2020-04-14 16:00:14.531 CEST [29578] STATEMENT:  select set_cluster_readonly();`
`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: ... done.`
`2020-04-14 16:00:14.531 CEST [29578] STATEMENT:  select set_cluster_readonly();`
`2020-04-14 16:00:14.531 CEST [29569] FATAL:  terminating connection due to conflict with recovery`
`2020-04-14 16:00:14.531 CEST [29569] DETAIL:  User query might have needed to see row versions that must be removed.`
`2020-04-14 16:00:14.531 CEST [29569] HINT:  In a moment you should be able to reconnect to the database and repeat your command.`


