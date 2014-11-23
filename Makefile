LDFLAGS := -L$(shell pg_config --libdir) -lpq
CFLAGS := -O2 -Wall -Werror
INCLUDES := -I$(shell pg_config --includedir)

PGLISTEN_SOURCES := pglisten.c
PGLISTEN_OBJS := $(PGLISTEN_SOURCES:%.c=%.o)

PGLATER_SOURCES := pglater.c
PGLATER_OBJS := $(PGLATER_SOURCES:%.c=%.o)

.PHONY: all
all: pglisten pglater

.PHONY: clean
clean: pglisten_clean pglater_clean
	rm -f *~

.PHONY: distclean
distclean: clean
	rm -f pglisten pglater

.PHONY: install
install: all
	@echo "There's no installation target."
	@echo "Just copy pglisten and pglater to wherever you need them."

.PHONY: pglisten_clean
pglisten_clean:
	rm -f $(PGLISTEN_OBJS)

pglisten: $(PGLISTEN_OBJS)
	$(CC) $< $(LDFLAGS) -o $@

pglisten.o: pglisten.c

pglater: $(PGLATER_OBJS)
	$(CC) $< $(LDFLAGS) -o $@

.PHONY: pglater_clean
pglater_clean:
	rm -f $(PGLATER_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
