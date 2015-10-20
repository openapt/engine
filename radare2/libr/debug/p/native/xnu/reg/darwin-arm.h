#if 0
ut32 gpr[13]
ut32 sp -- r13
ut32 lr -- r14
ut32 pc -- r15
ut32 cpsr -- program status
--> ut32[17]
// TODO: add
MMX: NEON
	ut128 v[32] // or 16 in arm32
	ut32 fpsr;
	ut32 fpcr;
VFP: FPU
	ut32 r[64]
	ut32 fpscr
#endif
	return strdup (
	"=pc	r15\n"
	"=sp	r13\n"
	"=bp	fp\n"
	"=a0	r0\n"
	"=a1	r1\n"
	"=a2	r2\n"
	"=a3	r3\n"
	"gpr	r0	.32	0	0\n"
	"gpr	r1	.32	4	0\n"
	"gpr	r2	.32	8	0\n"
	"gpr	r3	.32	12	0\n"
	"gpr	r4	.32	16	0\n"
	"gpr	r5	.32	20	0\n"
	"gpr	r6	.32	24	0\n"
	"gpr	r7	.32	28	0\n"
	"gpr	r8	.32	32	0\n"
	"gpr	r9	.32	36	0\n"
	"gpr	r10	.32	40	0\n"
	"gpr	r11	.32	44	0\n"
	"gpr	r12	.32	48	0\n"
	"gpr	r13	.32	52	0\n"
	"gpr	r14	.32	56	0\n"
	"gpr	r15	.32	60	0\n"
	"gpr	cpsr	.32	64	0\n"
	);
