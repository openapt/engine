OBJ_X86_OLLY=asm_x86_olly.o
OBJ_X86_OLLY+=../arch/x86/ollyasm/disasm.o
OBJ_X86_OLLY+=../arch/x86/ollyasm/asmserv.o
OBJ_X86_OLLY+=../arch/x86/ollyasm/assembl.o

STATIC_OBJ+=${OBJ_X86_OLLY}
TARGET_X86_OLLY=asm_x86_olly.${EXT_SO}

ifeq ($(WITHPIC),1)
ALL_TARGETS+=${TARGET_X86_OLLY}

${TARGET_X86_OLLY}: ${OBJ_X86_OLLY}
	${CC} $(call libname,asm_x86_olly) ${LDFLAGS} ${CFLAGS} -o ${TARGET_X86_OLLY} ${OBJ_X86_OLLY}
endif
