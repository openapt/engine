OBJ_MSIL=asm_msil.o
#OBJ_MSIL+=../arch/msil/demsil.o

STATIC_OBJ+=${OBJ_MSIL}
TARGET_MSIL=asm_msil.${EXT_SO}

ifeq ($(WITHPIC),1)
ALL_TARGETS+=${TARGET_MSIL}

${TARGET_MSIL}: ${OBJ_MSIL}
	${CC} $(call libname,asm_msil) ${LDFLAGS} ${CFLAGS} -o asm_msil.${EXT_SO} ${OBJ_MSIL}
endif
