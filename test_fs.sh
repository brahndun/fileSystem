#!/bin/bash
gcc -g -o formatfs formatfs.c softwaredisk.c
gcc -g -o testfs0 testfs0.c filesystem.c softwaredisk.c && ./formatfs && ./testfs0
gcc -g -o testfs1 testfs1.c filesystem.c softwaredisk.c && ./formatfs && ./testfs1
gcc -g -o testfs2 testfs2.c filesystem.c softwaredisk.c && ./formatfs && ./testfs2
gcc -g -o testfs3 testfs3.c filesystem.c softwaredisk.c && ./formatfs && ./testfs3
gcc -g -o testfs4a testfs4a.c filesystem.c softwaredisk.c && gcc -g -o testfs4b testfs4b.c filesystem.c softwaredisk.c && ./formatfs && ./testfs4a && ./testfs4b