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
#include "utils/memutils.h"
#if PG_VERSION_NUM <= 90600
#include "storage/lwlock.h"
#endif
#if PG_VERSION_NUM < 120000 
#include "access/transam.h"
#endif

#include "storage/ipc.h"
#include "storage/spin.h"
#include "miscadmin.h"
#include "storage/procarray.h"
#include "executor/executor.h"

PG_MODULE_MAGIC;

/*
 * has set_cluster_readonly() been executed 
 * in the current backend.
 */
static bool read_only_flag_has_been_set = false;

/*
 *
 * Global shared state
 * 
 */
typedef struct pgroSharedState
{
	LWLock	   	*lock;			/* self protection */
	bool		cluster_is_readonly;	/* cluster read-only global flag */
} pgroSharedState;

/* Saved hook values in case of unload */
#if PG_VERSION_NUM >= 150000
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorStart_hook_type prev_executor_start_hook = NULL;

/* Links to shared memory state */
static pgroSharedState *pgro= NULL;

static bool pgro_enabled = false;

/*---- Function declarations ----*/

void		_PG_init(void);
void		_PG_fini(void);

static void pgro_shmem_request(void);
static void pgro_shmem_startup(void);
static void pgro_shmem_shutdown(int code, Datum arg);
#if PG_VERSION_NUM < 140000
static void pgro_main(ParseState *pstate, Query *query);
#else
static void pgro_main(ParseState *pstate, Query *query, JumbleState *jstate);
#endif
static bool pgro_exec(QueryDesc *queryDesc, int eflags);

static bool pgro_set_readonly_internal();
static bool pgro_unset_readonly_internal();
static bool pgro_get_readonly_internal();

PG_FUNCTION_INFO_V1(pgro_set_readonly);
PG_FUNCTION_INFO_V1(pgro_unset_readonly);
PG_FUNCTION_INFO_V1(pgro_get_readonly);
	

/*
 * set cluster databases to read-only
 */

static bool pgro_set_readonly_internal()
{

	VirtualTransactionId *tvxid;
	TransactionId limitXmin = InvalidTransactionId;
	bool excludeXmin0 = false;
	bool allDbs = true;
	int excludeVacuum = 0;
	int nvxids;
	int i;
	pid_t pid;

	elog(LOG, "pg_readonly: killing all transactions ...");
	tvxid = GetCurrentVirtualXIDs(
                 limitXmin,
	         excludeXmin0, 
                 allDbs, 
                 excludeVacuum,
	         &nvxids);
	for (i=0; i < nvxids; i++)
	{
		/*
                 * No adequate ProcSignalReason found
                 */
		pid = CancelVirtualTransaction(
			tvxid[i],
                        PROCSIG_RECOVERY_CONFLICT_SNAPSHOT);
 		elog(LOG, "pg_readonly: PID %d signalled", pid);
	}
 	elog(LOG, "pg_readonly: ... done.");


	LWLockAcquire(pgro->lock, LW_EXCLUSIVE);
	pgro->cluster_is_readonly = true;
	LWLockRelease(pgro->lock);
	return true;
}


/*
 * set cluster databases to read write
 */

static bool pgro_unset_readonly_internal()
{
	LWLockAcquire(pgro->lock, LW_EXCLUSIVE);
	pgro->cluster_is_readonly = false;
	LWLockRelease(pgro->lock);
	return true;
}


/*
 * get cluster databases read-only or
 * read-write status
 */

static bool pgro_get_readonly_internal()
{
	bool val;

	LWLockAcquire(pgro->lock, LW_SHARED);
	val = pgro->cluster_is_readonly; 
	LWLockRelease(pgro->lock);
	return val;
}

/*
 * set cluster databases to read-only
 */
Datum pgro_set_readonly(PG_FUNCTION_ARGS)
{
	if (pgro_enabled == false)
	{
		ereport(ERROR, (errmsg("pg_readonly: pgro_set_readonly: pg_readonly is not enabled")));
		PG_RETURN_BOOL(false);
	}
	else 
	{
		elog(DEBUG5, "pg_readonly: pgro_set_readonly: entry");
		elog(DEBUG5, "pg_readonly: pgro_set_readonly: exit");
		read_only_flag_has_been_set = true;
		PG_RETURN_BOOL(pgro_set_readonly_internal());
	}
}

/*
 * set cluster databases to read-write
 */
Datum pgro_unset_readonly(PG_FUNCTION_ARGS)
{
	if (pgro_enabled == false)
	{
		ereport(ERROR, (errmsg("pg_readonly: pgro_unset_readonly: pg_readonly is not enabled")));
		PG_RETURN_BOOL(false);
	}
	else
	{
		elog(DEBUG5, "pg_readonly: pgro_unset_readonly: entry");
		elog(DEBUG5, "pg_readonly: pgro_unset_readonly: exit");
		read_only_flag_has_been_set = false;
		PG_RETURN_BOOL(pgro_unset_readonly_internal());
	}

}

/*
 * get cluster databases status 
 */
Datum pgro_get_readonly(PG_FUNCTION_ARGS)
{
	if (pgro_enabled == false)
	{
		ereport(ERROR, (errmsg("pg_readonly: pgro_get_readonly: pg_readonly is not enabled")));
		PG_RETURN_BOOL(false);
	}
	else 
	{
		elog(DEBUG5, "pg_readonly: pgro_get_readonly: entry");
		elog(DEBUG5, "pg_readonly: pgro_get_readonly: exit");
		PG_RETURN_BOOL(pgro_get_readonly_internal());
	}

}

/*
 ** Estimate shared memory space needed.
 *
 **/
static Size
pgro_memsize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(pgroSharedState));

	return size;
}

/*
 *
 * shmen_request_hook
 */
static void
pgro_shmem_request(void)
{
	/*
 	 * Request additional shared resources.  (These are no-ops if we're not in
 	 * the postmaster process.)  We'll allocate or attach to the shared
 	 * resources in pgls_shmem_startup().
	 */

#if PG_VERSION_NUM >= 150000
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();
#endif

	RequestAddinShmemSpace(sizeof(pgroSharedState));
#if PG_VERSION_NUM >= 90600
	RequestNamedLWLockTranche("pg_readonly", 1);
#endif

}


/*
 * shmem_startup hook: allocate or attach to shared memory.
 *
 */
static void
pgro_shmem_startup(void)
{
	bool		found;

	elog(DEBUG5, "pg_readonly: pgro_shmem_startup: entry");

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	/* reset in case this is a restart within the postmaster */
	pgro = NULL;


	/*
 	** Create or attach to the shared memory state
 	**/
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	pgro = ShmemInitStruct("pg_readonly",
			        pgro_memsize(),
			        &found);

	if (!found)
	{
		/* First time through ... */
#if PG_VERSION_NUM <= 90600
		RequestAddinLWLocks(1);
		pgro->lock = LWLockAssign();
#else
		pgro->lock = &(GetNamedLWLockTranche("pg_readonly"))->lock;
#endif
	
		pgro->cluster_is_readonly = false;
	}

	LWLockRelease(AddinShmemInitLock);

	/*
         * If we're in the postmaster (or a standalone backend...), set up a shmem
         * exit hook (no current need ???) 
         */ 
        if (!IsUnderPostmaster)
		on_shmem_exit(pgro_shmem_shutdown, (Datum) 0);

	/*
  	 * Done if some other process already completed our initialization.
  	 */
	if (found)
		return;

	elog(DEBUG5, "pg_readonly: pgro_shmem_startup: exit");

}

/*
 *
 *  shmem_shutdown hook
 *   
 *  Note: we don't bother with acquiring lock, because there should be no
 *  other processes running when this is called.
 */
static void
pgro_shmem_shutdown(int code, Datum arg)
{
	elog(DEBUG5, "pg_readonly: pgro_shmem_shutdown: entry");

	/* Don't do anything during a crash. */
	if (code)
		return;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!pgro)
		return;
	
	/* currently: no action */

	elog(DEBUG5, "pg_readonly: pgro_shmem_shutdown: exit");
}


/*
 * Module load callback
 */
void
_PG_init(void)
{

	const char *shared_preload_libraries_config;
        char *pg_readonly;
	
	elog(DEBUG5, "pg_readonly: _PG_init(): entry");

	shared_preload_libraries_config = GetConfigOption("shared_preload_libraries", true, false);
	pg_readonly = strstr(shared_preload_libraries_config, "pg_readonly");
	if (pg_readonly == NULL)
	{
		ereport(WARNING, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                  errmsg("pg_readonly: pg_readonly is not loaded")));
		pgro_enabled = false;
	}
	else
		pgro_enabled = true;

	if (pgro_enabled)
		elog(LOG, "pg_readonly:_PG_init(): pg_readonly extension is enabled");
	else	ereport(LOG, (errmsg("pg_readonly:_PG_init(): pg_readonly is not enabled")));


	/*
 	** Install hooks
	*/

	if (pgro_enabled)
	{
#if PG_VERSION_NUM >= 150000
		prev_shmem_request_hook = shmem_request_hook;
		shmem_request_hook = pgro_shmem_request;
#else
		pgro_shmem_request();
#endif
		prev_shmem_startup_hook = shmem_startup_hook;
		shmem_startup_hook = pgro_shmem_startup;
		prev_post_parse_analyze_hook = post_parse_analyze_hook;
		prev_executor_start_hook = ExecutorStart_hook;
		post_parse_analyze_hook = pgro_main;
 		ExecutorStart_hook = pgro_exec;	
	}	

	elog(DEBUG5, "pg_readonly: _PG_init(): exit");
}


/*
 *  Module unload callback
 */
void
_PG_fini(void)
{
	elog(DEBUG5, "pg_readonly: _PG_fini(): entry");

	/* Uninstall hooks. */
	shmem_startup_hook = prev_shmem_startup_hook;
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
	ExecutorStart_hook = prev_executor_start_hook;

	elog(DEBUG5, "pg_readonly: _PG_fini(): exit");
}

/*
 *
 */

static void
#if PG_VERSION_NUM < 140000
pgro_main(ParseState *pstate, Query *query)
#else
pgro_main(ParseState *pstate, Query *query, JumbleState *jstate)
#endif
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
			/*
                         * allow ROLLBACK
                         * for killed transactions
                         */
			if (
                              strstr((pstate->p_sourcetext), "rollback")
					|| 
                              strstr((pstate->p_sourcetext), "ROLLBACK")
                           )
			{
				elog(DEBUG1, "pg_readonly: pgro_main: query->querySource=%s",
                                     pstate->p_sourcetext);
				command_is_ro = true;
			}
			break;
		case CMD_NOTHING:
			kw = nokw;
			break;
		default:
			kw = kokw;
			break;
	  
	}
	elog(DEBUG1, "pg_readonly: pgro_main: query->commandType=%s", kw);
	elog(DEBUG1, "pg_readonly: pgro_main: command_is_ro=%d", command_is_ro);
	


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
		elog(DEBUG1, "pg_readonly: pgro_main: query->UtilityStmt=%s", stmt);
		elog(DEBUG1, "pg_readonly: pgro_main: command_is_ro=%d", command_is_ro);
	}

	if (pgro_get_readonly_internal() == true && command_is_ro == false)
		ereport(ERROR, (errmsg("pg_readonly: pgro_main: invalid statement because cluster is read-only")));
			

	if (prev_post_parse_analyze_hook)
#if PG_VERSION_NUM <= 140000
		(*prev_post_parse_analyze_hook)(pstate, query);	
#else
		(*prev_post_parse_analyze_hook)(pstate, query, jstate);	
#endif
	/* no "standard" call for else branch */

	elog(DEBUG5, "pg_readonly: pgro_main: exit");
}

static bool
pgro_exec(QueryDesc *queryDesc, int eflags)
{
	char *ops="select";
	char *opi="insert";
	char *opu="update";
	char *opd="delete"; 
	char *opo="other";
	char *op;
	bool command_is_ro = false;

	switch (queryDesc->operation)
	{
		case CMD_SELECT:
			op = ops;
			command_is_ro = true;
			break;
		case CMD_INSERT:
			op = opi;
			command_is_ro = false;
			break;
		case CMD_UPDATE:
			op = opu;
			command_is_ro = false;
			break;
		case CMD_DELETE:
			op = opd;
			command_is_ro = false;
			break;
		default:
			op=opo;
			command_is_ro = false;
			break;
		}

	elog(LOG, "pg_readonly: pgro_exec: qd->op %s", op);
	if (pgro_get_readonly_internal() == true && command_is_ro == false)
		ereport(ERROR, (errmsg("pg_readonly: pgro_exec: invalid statement because cluster is read-only")));

	if (prev_executor_start_hook)
                (*prev_executor_start_hook)(queryDesc, eflags);
	else	standard_ExecutorStart(queryDesc, eflags);

	return true;

}
