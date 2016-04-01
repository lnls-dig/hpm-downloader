CC=gcc
CFLAGS=-c
LDFLAGS=

#Directories
OBJ_DIR=obj/
SRC_DIR=src/
INC_DIR=inc/
LIB_DIR=lib/

#Files ------------------------------------------------------------------------

#MTCA
MTCA_SRC= $(wildcard $(SRC_DIR)*.c)
MTCA_OBJ=$(MTCA_SRC:$(SRC_DIR)%.c=$(OBJ_DIR)%.o)

#Libraries
MTCA_LIB= libmtca.a
MTCA_DLL= libmtca.dll

#Rules ------------------------------------------------------------------------
all: dirs $(MTCA_OBJ) $(MTCA_LIB)
dll: dirs $(MTCA_OBJ) $(MTCA_DLL)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)

$(MTCA_OBJ): $(OBJ_DIR)%.o : $(SRC_DIR)%.c
	@echo "Construction of $@ from $<"
	$(CC) $(CFLAGS) -I $(INC_DIR) $< -o $@
	@echo ""

$(MTCA_LIB) : $(MTCA_OBJ)
	@echo "Construction of MTCA Library"
	ar -q $(LIB_DIR)$(MTCA_LIB) $(MTCA_OBJ)
	@echo ""

$(MTCA_DLL) : $(MTCA_OBJ)
	@echo "Construction of MTCA DLL"
	gcc -shared -o $(LIB_DIR)$(MTCA_DLL) $(MTCA_OBJ)
	@echo ""

clean: mrproper
	@echo "removing objects"
	-rm $(OBJ_DIR)*.o
	@echo ""

	@echo "removing $(MTCA_LIB)"
	-rm $(LIB_DIR)$(MTCA_LIB)
	@echo ""

mrproper:
	@echo "Removing all *~ files"
	-find . -name "*~" -exec rm {} \;
	@echo ""
