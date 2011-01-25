/*  
** copyright/license: Only if this code is accepted and incorporated into the 
** normal, authoritative SQLite source code distribution from Richard Hipp --
** then his conditions of use apply, i.e. no license and no conditions of use 
** will apply and no attribution of authorship for these contributions are 
** asked for; and this source code module comment header may be replaced as needed.
**
** Otherwise, If this code is NOT accepted and NOT incorporated into the normal 
** code base of SQLite, as made available on www.sqlite.org, then I request 
** that attribution remain as, "Chris Wolf cw10025 gmail.com"; and would ask
** that this be maintained in derivative works, although this is not required.
** 
*/

#ifdef SQLITE_ENABLE_STOREDPROCS
// if the above define is false, this whole file is omitted

/** @file 
 ** This file contains functions implementing stored procedures.
 ** @author Chris Wolf cw10025 AT gmail.com
 ** @see parse.y build.c
 ** 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "sqliteInt.h"

/*
 ** The following DDL/DML statement templates form the data dictionary 
 ** for stored procudures.  Although I was able to create entries in
 ** sqlite_master for this purpose, for now, I am using separate tables
 ** to avoid unanticipated side effects.
 */
static const char *cr_sp_schema=
"create table if not exists sp_schema(\
 id integer primary key,\
 name text not null,\
 params_key text,\
 body text not null,\
 return_type int not null,\
 impl_lang text not null,\
 sql text not null, \
 unique(name, params_key))";

static const char *cr_sp_params=
"create table if not exists sp_params(\
  id integer primary key,\
  sp_schema_id int references sp_schema(id) on delete cascade,\
  name text not null,\
  type_decl text not null,\
  affinity char not null)";

static const char *ins_sp_schema=
"insert into sp_schema (name, params_key, body, return_type, impl_lang, sql)\
  values(?,?,?,?,?,?)";

static const char *ins_sp_params=
"insert into sp_params (sp_schema_id, name, type_decl, affinity)\
  values(?,?,?,?)";

static const char *del_sp_schema=
  "delete from sp_schema where name=?";
static const char *sel_sp_schema=
  "select * from sp_schema where name=?";
static const char *sel_sp_params=
  "select * from sp_params where sp_schema_id=?";
static const char *count_sp_schema=
  "select count(*) from sp_schema where name=?";
static const char *sel_sp_temp=
  "select tbl_name from sp_temp where tid=? and proc_name=? "
  "order by last_update_time desc limit 1";

/**
* Registry of language implementations connection init and finalize
* callbacks, as linked list.
*/
ProcLangImpl *pProcLangImpl;

/** Used by sp schmema data dictionary queries. */
struct query_ctx {
  char **data; /**< array of column data */
  int    nCols; /**< number of columns */
};
typedef struct query_ctx query_ctx;

/** mutator function for setting ConnProcCtx, on opaque sqlite3* connection type*/
void setDbProcCtx(sqlite3 *db, void *p) {
  db->pConnProcCtx = p;
}

/** accessor function for getting ConnProcCtx, on opaque sqlite3* connection type*/
void *getDbProcCtx(sqlite3 *db) {
  return db->pConnProcCtx;
}

static char * errmsgEx(sqlite3 *db, int rc, char *file, int lineno);
static int doUpdate(sqlite3 *db, const char  *sql, 
                    int *rowsEffected, char **ppzErrMsg);
static int addProcSchema(
  sqlite3          *db, 
  const char       *procName, 
  const proc_param *procParams,
  const char       *procBody,
  int               procReturnType,
  const char       *procLangImpl,
  const char       *sql,
  int              *rowsEffected,
  char            **ppzErrMsg); 

static int
countProc(
  sqlite3          *db,
  const char       *procName,
  int              *count,
  char            **ppzErrMsg /* must sqlite3_free*/);

static int
getProcSchema(
  sqlite3          *db, 
  const char       *procName, 
  char            **procParams,
  char            **procBody,
  int              *procReturnType,
  char            **procLangImpl,
  char            **ppzErrMsg /* must sqlite3_free*/);

#define MAX_SPPARAMS 128
#define SP_API_ERR(d,e,r,f,l) \
        *(e) = errmsgEx((d),(r),(f),(l));return 1

#define SP_API_ERR_INTRANS(d,s,e,r,f,l) \
        *(e) = errmsgEx((d),(r),(f),(l));\
        sqlite3_finalize((s));\
        doUpdate((d),"rollback", 0, 0);\
        return 1

int
spresult_push(SpResultset **head, Select *select) {
  SpResultset *entry = (SpResultset*)malloc(sizeof(SpResultset));
  if (entry == (SpResultset*)0)
    return SQLITE_NOMEM;
  entry->select = select;
  entry->next = *head==(SpResultset*)0 ? (SpResultset*)0 : *head;
  *head = entry;
}

Select *
spresult_pop(SpResultset **head) {
  if (*head == (SpResultset*)0)
    return (Select *)0;

  SpResultset *top = *head;
  Select *select = top->select;
  *head = top->next;
  free(top);
  return select;
}


/* 
** open connection callback - Called from main.c upon 
** constructing the sqlite3* connection. Any SP language-specific 
** initialization can occur by registering (adding to l
*/
int
sqlite3ProcDbInit(sqlite3 **pDb, char **pzErrMsg) {
  ProcLangImpl *p = pProcLangImpl;
  (*pDb)->pConnProcCtx = sqlite3DbMallocZero(*pDb, sizeof(ConnProcCtx));
  int rc;
  while(p) {
    if (p->procDbInit) {
      if((rc = (*p->procDbInit)(*pDb, pzErrMsg)) != SQLITE_OK)
        return rc;
    }
    p = p->pNext;
  }
  return SQLITE_OK;
}

/* close connection callback */
void
sqlite3ProcDbFinalize(sqlite3 *db) {
  sqlite3DbFree(db, db->pConnProcCtx);
  ProcLangImpl *p = pProcLangImpl;
  while(p) {
    if (p->procDbFinalize) {
      (*p->procDbFinalize)(db);
    }
    p = p->pNext;
  }
}

static char *
spResultTempTableName(Parse *pParse, Token *pProcName) {
  sqlite3 *db = pParse->db;
  u32 iRandom;
  sqlite3_randomness(sizeof(iRandom), &iRandom);
  char *pName = sqlite3MPrintf(db, "sp_%T_%08X", pProcName, iRandom&0x7fffffff);
  return pName;
}

void
sqlite3RenderResultSet(Parse *pParse, Select *s) {
  sqlite3 *db = pParse->db;
  if (spresult_push(&db->pConnProcCtx->pResultsetStack, s) == SQLITE_NOMEM)
    sqlite3ErrorMsg(pParse, "%s:%d - no memory.", __FILE__, __LINE__);
}

static void
sqlite3OutputResultSet(Parse *pParse) {
  sqlite3 *db = pParse->db;
  Select *s = 0;
  SelectDest dest = {SRT_Output, 0, 0, 0, 0};

  if ((s = spresult_pop(&db->pConnProcCtx->pResultsetStack)) == (Select*)0) {
    sqlite3ErrorMsg(pParse, "%s:%d - no resultset on stack.",
       __FILE__, __LINE__);
    return;
  }
  sqlite3Select(pParse, s, &dest);
  sqlite3SelectDelete(pParse->db, s);
}

int
sqlite3CreateProc(
  Parse      *pParse,
  Token      *pName1,   /* First part of the name of the proc */
  Token      *pName2,   /* Second part of the name of the proc*/
  const char *procBody,
  proc_param *procParams,
  int         procReturnType,
  const char *procLangImpl,
  int         nReplace,
  int         noErr
){
  char         *zName = 0; /* The name of the new proc */
  sqlite3      *db = pParse->db;
  ParseProcCtx *ctx = PARSE_PROC_CTX(pParse);
  int           iDb;         /* Database number to create the proc in */
  Token        *pName;    /* Unqualified name of the proc to create */
  char         *zErrMsg;
  char         *sql = sqlite3_malloc(ctx->sqlStrLen+1);
  
  (void)strncpy(sql, ctx->sqlStr, ctx->sqlStrLen);
  *(sql+ctx->sqlStrLen) = '\0';  

  /* see sqlite3StartTable(...) for name resolution info...*/
  iDb = sqlite3TwoPartName(pParse, pName1, pName2, &pName);
  if( iDb<0 ) return;

  pParse->sNameToken = *pName;
  zName = sqlite3NameFromToken(db, pName);
  if( zName==0 ) return;
  if( SQLITE_OK!=sqlite3CheckObjectName(pParse, zName) ){
    goto create_proc_error;
  }
#if 0 /* memory error upon closing without db file */
  {/* use internal lookup...*/
    Table *pTable;
    char *zDb = db->aDb[iDb].zName;
    if( SQLITE_OK!=sqlite3ReadSchema(pParse) ){
      goto create_proc_error;
    }
    pTable = sqlite3FindTable(db, "sp_schema", zDb);
    if( !pTable ){
#endif
      if(initSpSchema(pParse, &zErrMsg)!=SQLITE_OK) {
        sqlite3ErrorMsg(pParse, "%s", zErrMsg);
        goto create_proc_error;
      }
#if 0 /* memory error upon closing without db file */
    }
  } 
#endif
  
  int rowCount=-1;
  if(countProc(db, zName, &rowCount, &zErrMsg) != SQLITE_OK) {
      sqlite3ErrorMsg(pParse, "%s", zErrMsg);
      goto create_proc_error;
  }

  if (rowCount>0 && nReplace) {
    if(deleteProcSchema(db, zName, &rowCount, 0, &zErrMsg) != SQLITE_OK) {
      sqlite3ErrorMsg(pParse, "%s", zErrMsg);
      goto create_proc_error;
    }
    assert(rowCount == 1);
  } else if(rowCount>0 && noErr) {
      goto create_proc_error;
  }
  
  rowCount = -1;
  if(addProcSchema(db, zName, procParams, procBody, procReturnType,
    procLangImpl, (const char*)sql, &rowCount, &zErrMsg) != SQLITE_OK) {
      sqlite3_free(sql);
      sqlite3ErrorMsg(pParse, "%s", zErrMsg);
      goto create_proc_error;
  }
  sqlite3_free(sql);
  assert(rowCount == 1);

create_proc_error:
  sqlite3DbFree(db, zName);
  return;
}

void (*pf_sqlite3_execpython)(sqlite3 *, const char*);

void sqlite3ExecProc(
  Parse *pParse, 
  Token *pName1, 
  Token *pName2,
  ExprList *procArgs,
  Token *pReturn) {
  char    *zName = 0; /* The name of the proc to execute */
  sqlite3 *db = pParse->db;
  int      iDb;         /* Database number the proc is in */
  Token   *pName;    /* Unqualified name of the proc */
  char    *zErrMsg;
  char    *procParams;
  char    *procBody;
  int      procReturnType  = -1;
  char    *procLangImpl;
  int      rowsEffected = -1;

  /* see sqlite3StartTable(...) for name resolution info...*/
  iDb = sqlite3TwoPartName(pParse, pName1, pName2, &pName);

  if( iDb<0 ) return;

  pParse->sNameToken = *pName;
  zName = sqlite3NameFromToken(db, pName);

  if(getProcSchema(db, zName, &procParams, &procBody, &procReturnType,
    &procLangImpl, &zErrMsg) != SQLITE_OK) {
      sqlite3ErrorMsg(pParse, "%s", zErrMsg);
      return;
  } 

#ifdef SQLITE_USE_TEMPTABLES_FOR_PROCS
  if (SQLITE_SP_RESULTSET == procReturnType) {
    pParse->pExecProc->ResultTable.z = spResultTempTableName(pParse, pName);
    pParse->pExecProc->ResultTable.n = strlen(pParse->pExecProc->ResultTable.z);
  } 
   
  Token tt;
  tt.z = pParse->pExecProc->ResultTable.z;
  tt.n = strlen(tt.z);

  char *zSql = sqlite3MPrintf(db, 
    "insert into main.sp_temp (tid,proc_name,tbl_name,last_update_time) "
    "values(%x,%.*Q,%.*Q,%s)", 
     (long)db, pName->n, pName->z, tt.n, tt.z,"datetime('now')");

  if(doUpdate(db, zSql, &rowsEffected, &zErrMsg) != SQLITE_OK) {
    sqlite3ErrorMsg(pParse, "Error: %s:%d %s", __FILE__,__LINE__, zErrMsg);
    return;
  }
#endif

/*
  sqlite3DebugPrintf("*** ExecProc: %T %s %s %d %s %T\n", 
    pName, procParams, procBody, procReturnType, procLangImpl, 
    pParse->pExecProc->ResultTable);

  printf("*** ExecProc: %.*s %s %s %d %s %.*s\n", 
    pName->n, pName->z,
    procParams, procBody, procReturnType, procLangImpl, 
    pParse->pExecProc->ResultTable.n, pParse->pExecProc->ResultTable.z);
*/

  if (strcasecmp("sqlite",procLangImpl) == 0) {
    sqlite3NestedParse(pParse, "%s", procBody);
  } else if (strcasecmp("python",procLangImpl) == 0) {
    if(!pf_sqlite3_execpython) {
      char *zErrMsg = 0;
#ifdef PARANOID_EXTENSION_LOADING
      sqlite3ErrorMsg(pParse, 
        "Cannot execute \"%T\" - pyproc extension not loaded.", pName);
      return;
#else
      if (sqlite3_enable_load_extension(db, 1) 
        || sqlite3_load_extension(db, "libpyproc.dylib", 0, &zErrMsg)) {
        sqlite3ErrorMsg(pParse, "Cannot load pyproc extension %s",
          zErrMsg ? zErrMsg : "\"libpyproc.dylib\"");
        if (zErrMsg) sqlite3_free(zErrMsg);
        return;
      }
#endif
    }
 
    //pParse->nested++;
    (*pf_sqlite3_execpython)(db, procBody);
    //pParse->nested--;
  } 

  if(SQLITE_SP_RESULTSET == procReturnType) {
    sqlite3OutputResultSet(pParse);
  }
}

static char *
errmsgEx(sqlite3 *db, int rc, char *file, int lineno) {
  return sqlite3_mprintf("%s:%d error: %d - %s\n", 
    file, lineno, rc, sqlite3_errmsg(db));
}

static int 
sp_query_cb(void *uarg, int argc, char **argv, char **azColName){
  query_ctx *ctx = (query_ctx*)uarg;
  ctx->data = (char **)malloc(sizeof(char*)*argc);
  ctx->nCols = argc;
  int i; 
  for(i=0; i<argc; i++)
    ctx->data[i] = strdup(argv[i]?argv[i]:"");
   
  return 0;
}

static int 
query_boolean_cb(void *pInt, int argc, char **argv, char **azColName){
  if (argc>0 && argv && argv[0]) {
    *((int *)pInt) = argv[0][0] == '0' ? 0 : 1;
  } else {
    *((int *)pInt) = 0;
  }
  return 0;
}

static int
doUpdate(
  sqlite3     *db, 
  const char  *sql, 
  int         *rowsEffected, 
  char       **ppzErrMsg) {
  int rc = 0;
  if (rowsEffected) *rowsEffected = -1;
  if((rc = sqlite3_exec(db, sql, 0, 0, ppzErrMsg))
    != SQLITE_OK) {
      if (ppzErrMsg)
        fprintf(stderr, "%d: SQL error: %d - %s\n", __LINE__, rc, *ppzErrMsg);
      return 1;
  } else {
    if (rowsEffected) *rowsEffected = sqlite3_changes(db);
  }
  return 0;
}

static int
addProcSchema(
  sqlite3          *db, 
  const char       *procName, 
  const proc_param *procParams,
  const char       *procBody,
  int               procReturnType,
  const char       *procLangImpl,
  const char       *sql,
  int              *rowsEffected,
  char            **ppzErrMsg /* must sqlite3_free*/) {

  int               rc = 1;
  int               returnCode = 1;
  sqlite3_stmt     *pStmt;
  sqlite3_stmt     *pStmt2;
  const char       *pzTail = 0;
  char             *pProcParamsKey;
  char             *procParamsKeysMem[MAX_SPPARAMS];
  sqlite3_int64     sp_schema_id=0;

  *rowsEffected = -1;
  (void)memset(procParamsKeysMem, 0, MAX_SPPARAMS*sizeof(const char*));

  doUpdate(db, "begin transaction", rowsEffected, ppzErrMsg);

  if((rc = sqlite3_prepare_v2(db, ins_sp_schema, -1, &pStmt, &pzTail))
    != SQLITE_OK) {
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if((rc = sqlite3_bind_text(pStmt, 1, procName, -1, SQLITE_TRANSIENT)) 
    != SQLITE_OK) { 
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  procParamsKeysMem[0] = pProcParamsKey = strdup("");

  /* 
    concatenated param-name/param-type forms part of key 
    to permit overloaded proc names
  */
  proc_param *pp = (proc_param*)procParams;
  int i;
  for(i=1; pp && pp->name && i<=MAX_SPPARAMS*2; i+=2) {
    procParamsKeysMem[i] = strdup(pp->typeDecl);
    procParamsKeysMem[i+1] = strdup(",");
    (void)strcat(strcat(pProcParamsKey,procParamsKeysMem[i]),
          procParamsKeysMem[i+1]);
    pp = pp->pNext;
  }
  pProcParamsKey[strlen(pProcParamsKey)-1] = '\0';

  //printf("Paramkey: %s\n", pProcParamsKey); 
  if((rc = sqlite3_bind_text(pStmt, 2, pProcParamsKey, -1, SQLITE_TRANSIENT)) 
    != SQLITE_OK) { 
    for(i=0; i<MAX_SPPARAMS*2 && procParamsKeysMem[i] != 0; i++)
      free(procParamsKeysMem[i]);
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  for(i=0; i<MAX_SPPARAMS*2 && procParamsKeysMem[i] != 0; i++)
    free(procParamsKeysMem[i]);

  if((rc = sqlite3_bind_text(pStmt, 3, procBody, -1, SQLITE_TRANSIENT)) 
    != SQLITE_OK) { 
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if((rc = sqlite3_bind_int(pStmt, 4, procReturnType))
    != SQLITE_OK) { 
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if((rc = sqlite3_bind_text(pStmt, 5, procLangImpl, -1, SQLITE_TRANSIENT)) 
    != SQLITE_OK) { 
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }
  
  if((rc = sqlite3_bind_text(pStmt, 6, sql, -1, SQLITE_TRANSIENT)) 
    != SQLITE_OK) { 
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }  
  
  if((rc = sqlite3_step(pStmt)) != SQLITE_DONE) {
    SP_API_ERR_INTRANS(db,pStmt,ppzErrMsg,rc,__FILE__, __LINE__);
  }
  sqlite3_finalize(pStmt);

  sp_schema_id = sqlite3_last_insert_rowid(db);

  if((rc = sqlite3_prepare_v2(db, ins_sp_params, -1, &pStmt2, &pzTail))
    != SQLITE_OK) {
    SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  char aff[2];
  pp = (proc_param*)procParams;
  for(i=0; pp && pp->name && i<MAX_SPPARAMS; i++) {
    if((rc = sqlite3_bind_int64(pStmt2, 1, sp_schema_id))
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}
    if((rc = sqlite3_bind_text(pStmt2, 2, pp->name, -1, SQLITE_TRANSIENT)) 
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}
    if((rc = sqlite3_bind_text(pStmt2, 3, pp->typeDecl, -1, SQLITE_TRANSIENT)) 
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}

    aff[0] = pp->affinity;
    aff[1] = '\0';
    if((rc = sqlite3_bind_text(pStmt2, 4, aff, -1, SQLITE_TRANSIENT)) 
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}

    if((rc = sqlite3_step(pStmt2)) 
      != SQLITE_DONE) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}

    if((rc = sqlite3_reset(pStmt2))
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}
    if((rc = sqlite3_clear_bindings(pStmt2))
      != SQLITE_OK) {SP_API_ERR_INTRANS(db,pStmt2,ppzErrMsg,rc,__FILE__, __LINE__);}

    pp = pp->pNext;
  }

  sqlite3_finalize(pStmt2);

  doUpdate(db, "commit", rowsEffected, ppzErrMsg);
 
  return 0;
}

static int
getProcSchema(
  sqlite3          *db, 
  const char       *procName, 
  char            **procParams,
  char            **procBody,
  int              *procReturnType,
  char            **procLangImpl,
  char            **ppzErrMsg /* must sqlite3_free*/) {
  
  int               i;
  int               rc = 1;
  int               returnCode = 1;
  sqlite3_stmt     *pStmt;
  sqlite3_stmt     *pStmt2;
  const char       *pzTail = 0;
  char             *pProcParamsKey;
  char             *procParamsKeysMem[MAX_SPPARAMS];
  sqlite3_int64     sp_schema_id=0;
  query_ctx         ctx;

  ctx.data  = (char**)0;
  ctx.nCols = 0;

  char *zSql = malloc(strlen(sel_sp_schema)+strlen(procName)+2);
  strcpy(zSql, sel_sp_schema);
  *(zSql+strlen(zSql)-1) = '\0';
  (void)strcat(strcat(strcat(zSql,"'"),procName),"'");

  if((rc = sqlite3_exec(db, zSql, sp_query_cb, &ctx, ppzErrMsg))
    != SQLITE_OK) {
    free(zSql);
    SP_API_ERR(db,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if (ctx.data == (char**)0) {
    *ppzErrMsg = sqlite3MPrintf(db, "no such procedure \"%s\"", procName);
    rc = SQLITE_ERROR;
    goto exit_proc_schema;
  }

  *procParams     = ctx.data[2];
  *procBody       = ctx.data[3];
  *procReturnType = ctx.data[4]?(int)strtol(ctx.data[4],0,0):-1;
  *procLangImpl   = ctx.data[5];

exit_proc_schema:
  free(zSql);
  if (ctx.data)
    free(ctx.data);
  return rc;
}

static int
getSPResultsTableName(
  sqlite3          *db, 
  const char       *procName, 
  char            **resultsTableName,
  char            **ppzErrMsg /* must sqlite3_free*/) {
  
  int               i;
  int               rc = 1;
  int               returnCode = 1;
  sqlite3_stmt     *pStmt;
  sqlite3_stmt     *pStmt2;
  const char       *pzTail = 0;
  query_ctx         ctx;

  ctx.data  = (char**)0;
  ctx.nCols = 0;

  char *zSql = sqlite3MPrintf(db, 
    "select tbl_name from sp_temp where tid=%x and proc_name=%Q "
    "order by last_update_time desc limit 1",
     (long)db, procName);

printf(">>>>> %s", zSql);

  if((rc = sqlite3_exec(db, zSql, sp_query_cb, &ctx, ppzErrMsg))
    != SQLITE_OK) {
    free(zSql);
    SP_API_ERR(db,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if (ctx.data == (char**)0) {
    *ppzErrMsg = sqlite3MPrintf(db, 
      "no results temp table entry for \"%s\"", procName);
    rc = SQLITE_ERROR;
    goto exit_fetch_temptable;
  }

  *resultsTableName = ctx.data[2];

exit_fetch_temptable:
  free(zSql);
  if (ctx.data)
    free(ctx.data);
  return rc;
}

static int
countProc(
  sqlite3          *db,
  const char       *procName,
  int              *count,
  char            **ppzErrMsg /* must sqlite3_free*/) {

  int               rc = 1;
  query_ctx         ctx;

  (void)memset(&ctx, 0, sizeof(query_ctx));
  char *zSql = malloc(strlen(count_sp_schema)+strlen(procName)+2);
  strcpy(zSql, count_sp_schema);
  *(zSql+strlen(zSql)-1) = '\0';
  (void)strcat(strcat(strcat(zSql,"'"),procName),"'");

  if((rc = sqlite3_exec(db, zSql, sp_query_cb, &ctx, ppzErrMsg))
    != SQLITE_OK) {
    free(zSql);
    SP_API_ERR(db,ppzErrMsg,rc,__FILE__, __LINE__);
  }

  if (ctx.nCols == 1 && ctx.data)
    *count = ctx.data[0]?(int)strtol(ctx.data[0],0,0):-1;
  else
    *count = 0;

  free(zSql);
  free(ctx.data);
  return 0;
}


int
deleteProcSchema(
  sqlite3     *db, 
  const char  *procName, 
  int         *rowsDeleted, 
  int          noErr,
  char       **ppzErrMsg) {

  int          bRestoreFK=0;
  int          rc = 1;
  int          returnCode = 1;

  *rowsDeleted = -1;

  int bForeignKeys = -1;
  if((rc = sqlite3_exec(db, "pragma foreign_keys", 
    query_boolean_cb, &bForeignKeys, ppzErrMsg))
    != SQLITE_OK) {
      //fprintf(stderr, "%d: SQL error: %d - %s\n", __LINE__, rc, *ppzErrMsg);
      if (!noErr) 
        sqlite3Error(db, rc, "%s:%d error: %d, %s", __FILE__, __LINE__, rc, *ppzErrMsg);
      return 1;
  }

  if(bForeignKeys == 0 && 
    ((rc = sqlite3_exec(db, "pragma foreign_keys=on", 0, 0, ppzErrMsg))
    != SQLITE_OK)) {
      if (!noErr) 
        sqlite3Error(db, rc, "%s:%d error: %d, %s", __FILE__, __LINE__, rc, *ppzErrMsg);
      return 1;
  } else {
    bRestoreFK=1;
  }

  char *stmt = sqlite3_malloc(strlen(del_sp_schema)+1+strlen(procName)+2);
  (void)strcpy(stmt,del_sp_schema);
  *(stmt+strlen(stmt)-1) = '\0'; /* remove bind param '?' */
  (void)strcat(strcat(strcat(stmt,"'"), procName), "'");

  if((rc = sqlite3_exec(db, stmt, 0, 0, ppzErrMsg))
    != SQLITE_OK) {
      if (!noErr) 
        sqlite3Error(db, rc, "%s:%d error: %d, %s", __FILE__, __LINE__, rc, *ppzErrMsg);
      returnCode=1;
  } else {
      returnCode=0;
  }

  sqlite3_free(stmt);

  *rowsDeleted = sqlite3_changes(db);

  char *ignoredMsg;
  if (bRestoreFK) {
    if((rc = sqlite3_exec(db, "pragma foreign_keys=off", 0, 0, &ignoredMsg))
      != SQLITE_OK) {
        if (!noErr) 
          sqlite3Error(db, rc, "%s:%d error: %d, %s", __FILE__, __LINE__, rc, *ppzErrMsg);
    }
  }
  return returnCode;
}

int
initSpSchema(Parse *pParse, char **ppzErrMsg) {
  sqlite3 *db = pParse->db;
  int rowsEffected=-1;

  if(doUpdate(db, cr_sp_schema, &rowsEffected, ppzErrMsg) != SQLITE_OK 
    || rowsEffected < 0 
    || doUpdate(db, cr_sp_params, &rowsEffected, ppzErrMsg) != SQLITE_OK
    || rowsEffected < 0 
#ifdef SQLITE_USE_TEMPTABLES_FOR_PROCS
    || doUpdate(db, "create table if not exists main.sp_temp "
                    "(tid integer,proc_name text,"
                    " tbl_name text,last_update_time datetime)",
                    &rowsEffected, ppzErrMsg) != SQLITE_OK
    || rowsEffected < 0) 
#else
    )
#endif
    return SQLITE_ERROR;
}
#endif /* SQLITE_ENABLE_STOREDPROCS */
