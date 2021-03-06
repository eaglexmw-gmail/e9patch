#!/bin/sh

if [ -t 1 ]
then
    RED="\033[31m"
    GREEN="\033[32m"
    YELLOW="\033[33m"
    BOLD="\033[1m"
    OFF="\033[0m"
else
    RED=
    GREEN=
    YELLOW=
    BOLD=
    OFF=
fi

if [ $# -lt 1 ]
then
    echo "${YELLOW}usage${OFF}: $0 file.c EXTRA_ARGS" >&2
    exit 1
fi

CC=gcc
DIRNAME=`dirname $1`
BASENAME=`basename $1 .c`

shift

echo "$CC -fno-stack-protector -fpie -O2 -c -Wall $@ \"$DIRNAME/$BASENAME.c\""
if ! $CC -fno-stack-protector -fpie -O2 -c -Wall $@ "$DIRNAME/$BASENAME.c"
then
    echo >&2
    echo "${RED}error${OFF}: compilation of (${YELLOW}$BASENAME${OFF}) failed" >&2
    echo >&2
    exit 1
fi

echo "$CC \"$BASENAME.o\" -o \"$BASENAME\" -pie -nostdlib -Wl,-z -Wl,max-page-size=4096 -Wl,--export-dynamic -Wl,--entry=0x0 -Wl,--strip-all"
if !  $CC "$BASENAME.o" -o "$BASENAME" -pie -nostdlib -Wl,-z -Wl,max-page-size=4096 -Wl,--export-dynamic -Wl,--entry=0x0 -Wl,--strip-all
then
    echo >&2
    echo "${RED}error${OFF}: linking (${YELLOW}$BASENAME${OFF}) failed" >&2
    echo >&2
    exit 1
fi

RELOCS=`readelf -r "$BASENAME" | head -n 10 | grep 'R_X86_64_'`
if [ ! -z "$RELOCS" ]
then
    echo >&2
    echo "${RED}error${OFF}: the generated file (${YELLOW}$BASENAME${OFF}) contains relocations" >&2
    echo >&2
    echo "EXPLANATION:" >&2
    echo >&2
    echo "    E9Tool's call instrumentation does not support relocations.  These are" >&2
    echo "    usually caused by global variables that contain pointers, e.g.:" >&2
    echo >&2
    echo "      ${YELLOW}const char *days[] = {\"mon\", \"tue\", \"wed\", \"thu\", \"fri\", \"sat\", \"sun\"};${OFF}" >&2
    echo >&2
    echo "    Here, the global variable days[] is an array-of-pointers which usually" >&2
    echo "    results in relocations in the instrumentation binary.  Currently, E9Tool's" >&2
    echo "    call instrumentation does not apply relocations, meaning that the final" >&2
    echo "    patched binary using the instrumentation may crash." >&2
    echo >&2
    echo "    It may be possible to rewrite code to avoid relocations in exchange for" >&2
    echo "    extra padding, e.g.:" >&2
    echo >&2
    echo "      ${YELLOW}const char days[][4] = {\"mon\", \"tue\", \"wed\", \"thu\", \"fri\", \"sat\", \"sun\"};${OFF}" >&2
    echo >&2
    exit 1
fi

exit 0

