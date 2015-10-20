OBJ_CSR=asm_csr.o
#OBJ_CSR+=../arch/csr/dis.o

STATIC_OBJ+=${OBJ_CSR}
TARGET_CSR=asm_csr.${EXT_SO}

ifeq ($(WITHPIC),1)
ALL_TARGETS+=${TARGET_CSR}

${TARGET_CSR}: ${OBJ_CSR}
	${CC} $(call libname,asm_csr) ${LDFLAGS} ${CFLAGS} -o asm_csr.${EXT_SO} ${OBJ_CSR}
endif
