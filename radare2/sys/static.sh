#!/bin/sh

MAKE=make
gmake --help >/dev/null 2>&1
[ $? = 0 ] && MAKE=gmake

# find root
cd "$(dirname "$PWD/$0")" ; cd ..

ccache --help > /dev/null 2>&1
if [ $? = 0 ]; then
	[ -z "${CC}" ] && CC=gcc
	CC="ccache ${CC}"
	export CC
fi
PREFIX=/usr
if [ -n "$1" ]; then
	PREFIX="$1"
fi
DOBUILD=1
if [ 1 = "${DOBUILD}" ]; then
	# build
	if [ -f config-user.mk ]; then
		${MAKE} mrproper > /dev/null 2>&1
	fi
	export CFLAGS="-fPIC -pie "
#-D__ANDROID__=1"
	./configure-plugins
	./configure --prefix="$PREFIX" --with-nonpic --without-pic --disable-loadlibs
fi
${MAKE} -j 8
BINS="rarun2 rasm2 radare2 ragg2 rabin2 rax2 rahash2 rafind2 rasign2 r2agent radiff2"
# shellcheck disable=SC2086
for a in ${BINS} ; do
(
	cd binr/$a
	${MAKE} clean
	if [ "`uname`" = Darwin ]; then
		${MAKE} -j2
	else
		LDFLAGS=-static ${MAKE} -j2
	fi
	strip -s $a
)
done

rm -rf r2-static
mkdir r2-static
${MAKE} install DESTDIR="${PWD}/r2-static"
