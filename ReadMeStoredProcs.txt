Experimental Stored Procedure Implementation in SQLite 
(based on 3.7.3)

Chris Wolf, cw10025 AT gmail.com, 1 January 2011


Overview:

This non-official release contains a proof-of-concept implementation
of stored procedures for SQLite.  This implementation is most certainly 
not suitable for actual production use and even hardly usable for casual 
experimentation!

The grammar additions are pretty solid (in my opinion) but the 
underlying code is incomplete – for example, even though procedure
parameter definitions are stored in the schema and invocation arguments 
are parsed – actually passing argument values to the stored proc is NOT 
implemented yet.  Also, may be memory leaks, memory faults, etc. 

The idea is to be able to support multiple stored procedure language 
implementations, where each implementation except the special  'sqlite', 
are implemented as SQLite extensions, which embed    
interpreted languages – e.g. Python, Lua, Perl, etc. 

Language-specific dispatch is keyed off the 'LANGUAGE' specifier in the 
"CREATE PROCEURE" DDL statement and is stored as metadata in the stored 
proc schema table. ('sp_schema')

SQLite Grammar Additions for Stored Procedures:

The grammar to create a stored procedure is similar to the 
grammar used by PostgreSQL for user-defined function creation:

CREATE [ OR REPLACE ] PROC[EDURE] [ IF NOT EXISTS ]
[dbname.] procedure_name ( [ [ argname ] argtype [, ...] ] )
    [ RETURNS rettype ] AS $$ 
    'proc body text' 
    $$
    LANGUAGE langname
  
The key feature here, is that the whole body of the stored procedure 
is placed between two double dollar-signs, used as a delimiter of a
block of opaque text - this approach makes it easy to treat the whole
of the stored procedure body text as a single token to the lexical 
scanner (a/k/a tokenizer, lexer).

Other grammars can do without the '$$'....'$$' escape trick,
by implementing "start states" in the lexical scanner.  Since 
this is a POC and another major db vendor (PostegreSQL) uses the 
escape characters trick, that's how I did it.
  
    
The grammar to execute a stored procedure is similar to Sybase
(or MS SQLServer):

exec[ute] [@return_status  = ]
     [dbname.]procedure_name
          [[@parameter_name =] value | 
               [@parameter_name =] @variable
          [, [@parameter_name =] value | 
               [@parameter_name =] @variable] 

Note that this implementation mandates that stored procedures can only be 
invoked in a statement, not an expression. (just like Sybase/SQL Server, 
but unlike PostgreSQL, whose user-defined functions can be invoked in an 
expression)  This limitation makes the implementation easier.

Also note that even though the grammar can consume variable definitions, 
for example, return status, and expressions with variables – in SQLite 
itself, there is no concept of session variables, so these constructs 
will bomb if you attempt to use them.
               
The grammar to drop a stored procedure is also similar to Sybase:

drop proc[edure] [dbname.] procedure_name

    
In this release, there are only two language implementations:
'sqlite' and 'python'.  'sqlite' simply indicates that the stored 
procedure body is a semicolon-delimited batch of sql statements.

As expected, there are limitations, for example, with language type 
'sqlite', if multiple select statements are batched together, they 
must all return the same number of columns.
  

The 'sqlite' language implementation executes batches of semicolon-delimited 
stored sql statements via the internal routine, 'sqlite3NestedParse'.  
Since the SQLite grammar does not implement control-flow statements, 
the utility of language type 'sqlite' is limited.

Here is a basic example:

$ ./sqlite3 test.db
SQLite version 3.7.3.p1
Enter ".help" for instructions
Enter SQL statements terminated with a ";"
sqlite> create proc foo() as $$select 'foo'$$ language sqlite;
sqlite> exec foo();
foo
sqlite> drop proc foo;
sqlite> 

The Python language implementation is accomplished by using a modified 
version of Gerhard Häring's PySqlite, which is an implementation of the 
Python Database API 2.0.  I believe this approach is better then using 
the APSW API, since the Python DBAPI 2.0 spec is database neutral, thus 
porting database application logic to python stored procedures will be 
more streamlined. (in case this crazy idea ever gets fully realized)
I also had problems hacking the APSW module to accept a sqlite3* pointer,
which is another reason I used the pysqlite2 module instead.

Here is an example of a Python stored procedure:
(using old-style exception handling to support Python-2.4
 on MacOSX)

create or replace proc pytest3() returns resultset as $$
  from pysqlite2 import dbapi2 as sqlite3
  import sys
  con = sqlite3.connect()
  con.row_factory = sqlite3.Row
  cur = con.cursor()
  try:
      try:
          cur.execute('spresult select * from sqlite_master')
      except sqlite3.OperationalError, (errmsg):
          print "sqlite3.OperationalError: %s" % (errmsg)
      except sqlite3.ProgrammingError, (errmsg):
          print "sqlite3.ProgrammingError: %s" % (errmsg)
      except:
          print "Unexpected error: %s" % sys.exc_info()[0]
  finally:
      try:
          # releases recources, doesn't actually close sqlite3* handle
          con.close()  
      except:
          print "Unexpected error closing cursor: %s", sys.exc_info()[0]
$$ language python;


This implementation for Python stored procs is accomplished by  creating a 
Python interpreter instance; then taking the current connection, (of type sqlite3*) 
and sticking this pointer value into a Python global variable named “sqlite3_db_handle”, 
so that the modified pysqlite2 module can access it when it instantiates a 
“dbapi2.connection” with zero arguments. (Normally, at least one argument is required)  
The embedded interpreter is provided via a SQLite extension (loadable shared library).  
This is a scalable approach since the core code does not need to be recompiled for 
each new language implementation, assuming certain data structures are implemented 
by the extension.

Building:

The “runconfigure.sh” script is just a one-liner:

../configure --enable-load-extension --enable-debug –enable-stored-procedures


$ git clone git://github.com/wolfch/sqlite-3.7.3
$ cd sqlite-3.7.3
$ git checkout sproc-1
$ mkdir ./build
$ cp runconfigure.sh ./build
$ cd ./build
$ ./runconfigure.sh
$ export LIBS=-ldl

(this last env setting only required on Linux, not MacOSX - I have not tried building on Windoze)

$ make


$ cd ../../
$ git clone git://github.com/wolfch/pysqlite-2.6.0
$ cd pysqlite-2.6.0
$ git checkout sproc-1
$ sudo python setup.py install


To build the Python stored proc extension:

On Linux, if you don't have the python-dev package (separate from runtime):
$ sudo apt-get install python2.6-dev

On MacOSX, it comes with Python-2.4 and the dev libraries and headers already.

$ cd sqlite-3.7.3/ext/pyproc
$ make -f Makefile.darwin  (on Mac)
$ make -f Makefile.linux   (on Linux)

Generating code documentation using "doxygen"
(see http://www.doxygen.org)

$ cd sqlite-3.7.3
$ doxygen
[lots of output... the directory "codegen" is created and populated]

Open: codedoc/html/index.html

Precompiled binaries are checked in for MacOSX and Linux in
./bin/darmin and ./bin/linux, respectively.

