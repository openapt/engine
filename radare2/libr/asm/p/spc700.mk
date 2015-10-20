OBJ_SPC700=asm_spc700.o

STATIC_OBJ+=${OBJ_SPC700}
TARGET_SPC700=asm_spc700.${EXT_SO}

ifeq ($(WITHPIC),1)
ALL_TARGETS+=${TARGET_SPC700}

${TARGET_SPC700}: ${OBJ_SPC700}
	${CC} ${call libname,asm_spc700} ${CFLAGS} -o ${TARGET_SPC700} ${OBJ_SPC700}
endif
