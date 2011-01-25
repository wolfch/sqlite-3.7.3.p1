#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

const char *cpSql =
"create or replace proc pytest3()\n"
"returns resultset as $$\n"
"from pysqlite2 import dbapi2 as sqlite3\n"
"import sys\n"
"con = sqlite3.connect()\n"
"#con = sqlite3.connect('/Users/cwolf/src/sqlite-3.7.3.p1/build/demo.sqlite')\n"
"con.row_factory = sqlite3.Row\n"
"cur = con.cursor()\n"
"try:\n"
"  cur.execute('spresult select * from dept')\n"
"except sqlite3.OperationalError, (errmsg):\n"
"  con.close()\n"
"  print \"sqlite3.OperationalError: %s\" % (errmsg)\n"
"except sqlite3.ProgrammingError, (errmsg):\n"
"  con.close()\n"
"  print \"sqlite3.ProgrammingError: %s\" % (errmsg)\n"
"except:\n"
"  con.close()\n"
"  print \"Unexpected error: \" % sys.exc_info()[0]\n"
"\n"
"try:\n"
"  con.close()\n"
"except:\n"
"  \"Unexpected error closing cursor:\", sys.exc_info()[0]\n"
"$$ language python;\n";

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
  int i;
  for(i=0; i<argc; i++){
    printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  }
  printf("\n");
  return 0;
}

int main(int argc, char **argv){
  sqlite3 *db;
  char *zErrMsg = 0;
  int rc;

  if( argc != 2 ){
    fprintf(stderr, "Usage: %s DATABASE\n", argv[0]);
    exit(1);
  }

  rc = sqlite3_open(argv[1], &db);
  if( rc ){
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    exit(1);
  }

  if((rc = sqlite3_enable_load_extension(db, 1)) != SQLITE_OK) {
    fprintf(stderr, "Can't enable load_extension\n");
    sqlite3_close(db);
    exit(1);
  }

  if((rc = sqlite3_load_extension(db, "libpyproc.dylib", 0, &zErrMsg))
    != SQLITE_OK) {
    fprintf(stderr, "Can't open load libpyproc: %s\n", zErrMsg);
    sqlite3_close(db);
    exit(1);
  }

  rc = sqlite3_exec(db, cpSql, callback, 0, &zErrMsg);
  if( rc!=SQLITE_OK ){
    fprintf(stderr, "SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
  }
  sqlite3_close(db);
  return 0;
}
