#!/bin/sh
EXIT=0
OUTPUT="loop
loop
loop"
cat > t.r <<EOF
exit@syscall(1);
write@syscall(4);

main();

fun2@(128) {
	write (.arg0, .arg4, .arg8);
}

fun@(128) {
	fun2 (1, .arg0, .arg4);
}
main@global(128,128) {
	.var0 = 0;
	while (.var0<2) {
		fun ("loop\n", 5);
		.var0 += 1;
	}
	exit (0);
}
EOF
. ./t.sh
