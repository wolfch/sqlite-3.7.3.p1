#include <Python.h>
#include "sqlite3.h"
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

void 
execpython(sqlite3 *db, const char *procBody) {
  PyObject* main_module;
  PyObject* global_dict; 
  PyObject* pydb = PyInt_FromLong((long)db);

  main_module = PyImport_AddModule("__main__");
  global_dict = PyModule_GetDict(main_module);


  if(PyDict_SetItemString(global_dict, "sqlite3_db_handle", pydb) < 0) {
    Py_DECREF(pydb);
    fprintf (stderr,
      "doPyProc() error: cannot set item in global dict.");
    return;
  }

  PyRun_SimpleString(procBody);
  Py_DECREF(pydb);
}

static void
_doPyProc(sqlite3_context *context, 
         int argc,
         sqlite3_value ** argv) {
  sqlite3 *db = sqlite3_context_db_handle(context);
  const char *procbody=(const char*)0;

  PyObject* main_module;
  PyObject* global_dict; 
  PyObject* pydb = PyInt_FromLong((long)db);

  main_module = PyImport_AddModule("__main__");
  global_dict = PyModule_GetDict(main_module);


  if(PyDict_SetItemString(global_dict, "sqlite3_db_handle", pydb) < 0) {
    Py_DECREF(pydb);
    fprintf (stderr,
      "doPyProc() error: cannot set item in global dict.");
    return;
  }

  if (sqlite3_value_type (argv[0]) != SQLITE_TEXT) {
    fprintf (stderr,
      "doPyProc() error: argument 1 [procbody] is not of type String\n");
    sqlite3_result_int (context, 0);
    return;
  }
  procbody = (const char*)sqlite3_value_text(argv[0]);
  PyRun_SimpleString(procbody);
  Py_DECREF(pydb);
  sqlite3_result_int (context, 1);
}

static void
pid(sqlite3_context *context, 
         int argc,
         sqlite3_value ** argv) {
  sqlite3 *db    = sqlite3_context_db_handle(context);
  pid_t    pid = getpid();
  char *pidStr;

  if (argc!=0){
    fprintf (stderr, "pid() takes no arguments.\n");
    return;
  }
  pidStr = sqlite3_mprintf("%08.08x", (long)pid);

  sqlite3_result_text(context, pidStr, -1, SQLITE_TRANSIENT);
  sqlite3_free(pidStr);
}

static void
sid(sqlite3_context *context, 
         int argc,
         sqlite3_value ** argv) {
  sqlite3 *db    = sqlite3_context_db_handle(context);
  char *sidStr;

  if (argc!=0){
    fprintf (stderr, "sid() takes no arguments.\n");
    return;
  }
  sidStr = sqlite3_mprintf("%08.08x", (long)db);

  sqlite3_result_text(context, sidStr, -1, SQLITE_TRANSIENT);
  sqlite3_free(sidStr);
}

extern void (*pf_sqlite3_execpython)(sqlite3*, const char*);

int
sqlite3_extension_init(
  sqlite3 *db,          /* The database connection */
  char **pzErrMsg,      /* Write error messages here */
  const sqlite3_api_routines *pApi  /* API methods */
){
  SQLITE_EXTENSION_INIT2 (pApi);
  pf_sqlite3_execpython = execpython;
  fprintf(stderr, "*** Database: 0x%08.08x\n", (long)db);
  Py_Initialize();
  PyThreadState* gTstate  = PyThreadState_Get();
  PyThreadState* pyTstate = Py_NewInterpreter();
  // void Py_EndInterpreter(PyThreadState *pyTstate)

  fprintf(stderr, 
    "*** global interpreter: 0x%08.08x sub-interpreter: 0x%08.08x\n", 
      (long)gTstate, (long)pyTstate);

  sqlite3_create_function(db, "sid", 0, SQLITE_ANY, 0, sid, 0, 0);
  sqlite3_create_function(db, "pid", 0, SQLITE_ANY, 0, pid, 0, 0);
  return sqlite3_create_function(db, "pyexec", 1, SQLITE_ANY, 0, _doPyProc, 0, 0);
}
