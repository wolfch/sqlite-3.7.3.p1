# do not run from top level - run from "build" dir:
# i.e., perform these steps:
# $ mkdir ./build
# $ cp runconfigure.sh ./build
# $ cd ./build
# $ ./runconfigure.sh
#
../configure --enable-load-extension --enable-debug --enable-stored-procedures
#../configure --enable-load-extension --enable-debug 
#../configure --enable-load-extension --disable-amalgamation
