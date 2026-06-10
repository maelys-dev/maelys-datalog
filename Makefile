CC = clang
CFLAGS = -Wall -Wextra -g -I.
LIBS =

ENGINE_SRCS = \
	src/core/maelys_datalog_audit.c \
	src/core/maelys_datalog_diagnostic.c \
	src/core/maelys_datalog_domain_registry.c \
	src/core/maelys_datalog_edb.c \
	src/core/maelys_datalog_lexer.c \
	src/core/maelys_datalog_parser.c \
	src/core/maelys_datalog_policy.c \
	src/core/maelys_datalog_predicate_registry.c \
	src/core/maelys_datalog_symbol_table.c \
	src/manifest/maelys_datalog_manifest_buffer.c \
	src/manifest/maelys_datalog_manifest_file.c

COMMON_SRCS = \
	common/maelys_sha256.c \
	common/maelys_utf8.c \
	vendor/yyjson/yyjson.c

SRCS = $(ENGINE_SRCS) $(COMMON_SRCS)
OBJS = $(SRCS:.c=.o)

EXAMPLES_SRCS  = examples/domains/maelys_datalog_example_domains.c
EXAMPLES_CHECK = examples/maelys_datalog_examples_check.c
EXAMPLES_OBJS  = $(EXAMPLES_SRCS:.c=.o)
EXAMPLES_BIN   = examples/maelys_datalog_examples_check

libmaelys_datalog.a: $(OBJS)
	ar rcs $@ $^

examples: libmaelys_datalog.a $(EXAMPLES_OBJS) $(EXAMPLES_CHECK)
	$(CC) $(CFLAGS) $(EXAMPLES_OBJS) $(EXAMPLES_CHECK) -L. -lmaelys_datalog $(LIBS) -o $(EXAMPLES_BIN)
	./$(EXAMPLES_BIN)

clean:
	rm -f $(OBJS) libmaelys_datalog.a
	rm -f $(EXAMPLES_OBJS) $(EXAMPLES_BIN)
	rm -f examples/maelys_datalog_examples_check.o
