OBJ_6502PSEUDO+=parse_6502_pseudo.o

TARGET_6502PSEUDO=parse_6502_pseudo.${EXT_SO}
STATIC_OBJ+=${OBJ_6502PSEUDO}
LIBDEPS=-L../../util -lr_util
LIBDEPS+=-L../../flags -lr_flags

ifeq ($(WITHPIC),1)
ALL_TARGETS+=${TARGET_6502PSEUDO}
${TARGET_6502PSEUDO}: ${OBJ_6502PSEUDO}
	${CC} $(call libname,parse_6502_pseudo) ${LIBDEPS} $(LDFLAGS) \
		-shared ${CFLAGS} -o ${TARGET_6502PSEUDO} ${OBJ_6502PSEUDO}
endif
