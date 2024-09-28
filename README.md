# pg_readonly
pg_readonly is a PostgreSQL extension which allows to set all cluster databases read only.


# Installation
## Compiling

This module can be built using the standard PGXS infrastructure. For this to work, the pg_config program must be available in your $PATH:

`git clone https://github.com/pierreforstmann/pg_readonly.git` <br>
`cd pg_readonly` <br>
`make` <br>
`make install` <br>

This extension has been validated with PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16 and 17.

## PostgreSQL setup

Extension must be loaded at server level with `shared_preload_libraries` parameter:
<br> <br>
`shared_preload_libraries = 'pg_readonly'`
<br><br>
and it must be created with following SQL statement at server level:
<br><br>
`create extension pg_readonly;`
<br>


## Usage
pg_readonly has no specific GUC. <br><br>
The read-only status is managed only in (shared) memory with a global flag. SQL functions are provided to set the flag, to unset the flag and to query the flag.
The current version of the extension does not allow to store the read-only status in a permanent way.<br><br>
The flag is at cluster level: either all databases are read-only or all database are read-write (the usual setting).<br><br>
The read-only mode is implemented by filtering SQL statements: 
<ul>
<li>SELECT statements are allowed if they don't call functions that write. </li>
<li>DML (INSERT, UPDATE, DELETE) and DDL statements including TRUNCATE are forbidden entirely. </li>
<li> DCL statements GRANT and REVOKE are also forbidden. </li>
</ul>
This means that the databases are in read-only mode at SQL level: however, the checkpointer, background writer, walwriter and the autovacuum launcher are still running; this means that the database files are not read-only and that in some cases the database may still write to disk.<br>


## Example

To query the cluster status, call the function get_cluster_readonly which returns true is the cluster is read-only and false if not: <br>

`# select get_cluster_readonly();`<br>
` get_cluster_readonly `<br>
`----------------------`<br>
` f`<br>
`(1 row)`<br>

To set the cluster read-only, call the function set_cluster_readonly:<br>
`# select set_cluster_readonly();`<br>
` set_cluster_readonly ` <br>
`----------------------` <br>
` t` <br>
`(1 row)`

The cluster is now read-only and only SELECT statements are allowed:

`pierre=# select * from t;`<br>
` x  |  y  `<br>
`----+-----`<br>
` 32 | abc`<br>
`(1 row)`<br>

`# update t set x=33 where y='abc';`<br>
`ERROR:  pg_readonly: invalid statement because cluster is read-only`<br>
`# select 1 into tmp;`<br>
`ERROR:  pg_readonly: invalid statement because cluster is read-only`<br>
`# create table tmp(c text);`<br>
`ERROR:  pg_readonly: invalid statement because cluster is read-only`<br>

To set the cluster on read-write, call the function unset_cluster_readonly:

`# select unset_cluster_readonly();`<br>
` unset_cluster_readonly `<br>
`------------------------`<br>
` t`<br>
`(1 row)`<br>

The cluster is now read-write and any DML or DDL statement is allowed:<br>
`# update t set x=33 where y='abc';`<br>
`UPDATE 1`<br>
`# select * from t;`<br>
` x  |  y  `<br>
`----+-----`<br>
` 33 | abc`<br>
`(1 row)`<br>

Note that any open transaction is cancelled by set_cluster_readonly function.<br>
The client is disconnected and gets the following message: <br>
`FATAL:  terminating connection due to conflict with recovery`<br>
`DETAIL:  User query might have needed to see row versions that must be removed.`<br>
`HINT:  In a moment you should be able to reconnect to the database and repeat your command.`<br>
In PostgreSQL log, following messages are written:<br>

`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: killing all transactions ...`<br>
`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: PID 29569 signalled`<br>
`2020-04-14 16:00:14.531 CEST [29578] LOG:  pg_readonly: ... done.`<br>
`2020-04-14 16:00:14.531 CEST [29569] FATAL:  terminating connection due to conflict with recovery`<br>
`2020-04-14 16:00:14.531 CEST [29569] DETAIL:  User query might have needed to see row versions that must be removed.`<br>
`2020-04-14 16:00:14.531 CEST [29569] HINT:  In a moment you should be able to reconnect to the database and repeat your command.`<br>


