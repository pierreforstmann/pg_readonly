/*-------------------------------------------------------------------------
 *  
 * pg_readonly is a PostgreSQL extension which allows to set a whole
 * cluster read only: no INSERT,UPDATE,DELETE and no DDL can be run.
 *  
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *          
 * Copyright (c) 2020, Pierre Forstmann.
 *            
 *-------------------------------------------------------------------------
*/
#include "postgres.h"
#include "parser/analyze.h"
#include "nodes/nodes.h"
#include "storage/proc.h"
#include "access/xact.h"

#include "tcop/tcopprot.h"
#include "tcop/utility.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"

PG_MODULE_MAGIC;

static bool pgro_is_enabled = true;

/* Saved hook values in case of unload */
post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;

/*---- Function declarations ----*/

void		_PG_init(void);
void		_PG_fini(void);

static void pgro_main(ParseState *pstate, Query *query);

/*
 * Module load callback
 */
void
_PG_init(void)
{
	elog(DEBUG5, "pg_readonly: _PG_init(): entry");

	pgro_is_enabled = true;
	ereport(LOG, (errmsg("pg_readonly:_PG_init(): pg_readonly is enabled")));
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = pgro_main;
		
	elog(DEBUG5, "pg_readonly: _PG_init(): exit");
}


/*
 *  Module unload callback
 */
void
_PG_fini(void)
{
	elog(DEBUG5, "pg_readonly: _PG_fini(): entry");
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
	elog(DEBUG5, "pg_readonly: _PG_fini(): exit");
}

/*
 *
 */

static void
pgro_main(ParseState *pstate, Query *query)
{

	char	*sekw = "SELECT";
	char	*inkw = "INSERT";
	char	*upkw = "UPDATE";
	char	*dekw = "DELETE";
	char	*utkw = "UTILITY";
	char	*nokw = "NOTHING";
	char	*unkw = "UNKNOWN";
	char	*kokw = "???????";
	char	*kw = NULL;
	char	*expstmt = "EXPLAIN";
	char	*setvstmt = "SET";
	char	*showvstmt = "SHOW";
	char	*prepstmt = "PREPARE";
	char	*execstmt = "EXECUTE";
	char	*deallocstmt = "DEALLOC";
	char	*otherstmt = "OTHER";
	char	*stmt = NULL;
	bool	command_is_ro = false;
	
	elog(DEBUG5, "pg_readonly: pgro_main entry");

	switch (query->commandType) {
		case CMD_UNKNOWN:
			kw = unkw;
			break;
		case CMD_SELECT:
			command_is_ro = true;
			kw = sekw;
			break;
		case CMD_UPDATE:
			kw = upkw;
			break;
		case CMD_INSERT:
			kw = inkw;
			break;
		case CMD_DELETE:
			kw = dekw;
			break;
		case CMD_UTILITY:
			kw = utkw;
			break;
		case CMD_NOTHING:
			kw = nokw;
			break;
		default:
			kw = kokw;
			break;
	  
	}
	ereport(DEBUG1, (errmsg("pg_readonly: pgro_main: query->commandType=%s", kw)));
	ereport(DEBUG1, (errmsg("pg_readonly: pgro_main: command_is_ro=%d", command_is_ro)));

	if (query->commandType == CMD_UTILITY)
	{
		switch ((nodeTag(query->utilityStmt)))
		{
			case T_ExplainStmt: 
				stmt = expstmt;
				command_is_ro = true;
				break;
			case T_VariableSetStmt: 
				stmt = setvstmt;
				command_is_ro = true;
				break;
			case T_VariableShowStmt: 
				stmt = showvstmt;
				command_is_ro = true;
				break;
			case T_PrepareStmt: 
				stmt = prepstmt;
				command_is_ro = true;
				break;
			case T_ExecuteStmt: 
				stmt = execstmt;
				command_is_ro = true;
				break;
			case T_DeallocateStmt: 
				stmt = deallocstmt;
				command_is_ro = true;
				break;
			default:
				stmt = otherstmt;
				break;
		}
		ereport(DEBUG1, (errmsg("pg_readonly: pgro_main: query->UtilityStmt=%s", stmt)));
		ereport(DEBUG1, (errmsg("pg_readonly: pgro_main: command_is_ro=%d", command_is_ro)));
	}

	if (!command_is_ro)
		ereport(ERROR, (errmsg("pg_readonly: invalid statement because cluster is read-only")));
			

	if (prev_post_parse_analyze_hook)
		(*prev_post_parse_analyze_hook)(pstate, query);	
	/* no "standard" call for else branch */

	elog(DEBUG5, "pg_readonly: pgro_main: exit");
}


