CC = clang
CFLAGS = -Wall -Wextra -g -I.
LIBS =

ENGINE_SRCS = \
	policy/maelys_datalog_audit.c \
	policy/maelys_datalog_diagnostic.c \
	policy/maelys_datalog_domain_registry.c \
	policy/maelys_datalog_edb.c \
	policy/maelys_datalog_lexer.c \
	policy/maelys_datalog_manifest_buffer.c \
	policy/maelys_datalog_manifest_file.c \
	policy/maelys_datalog_parser.c \
	policy/maelys_datalog_policy.c \
	policy/maelys_datalog_predicate_registry.c \
	policy/maelys_datalog_symbol_table.c

COMMON_SRCS = \
	common/maelys_sha256.c \
	common/maelys_utf8.c \
	vendor/yyjson/yyjson.c

SRCS = $(ENGINE_SRCS) $(COMMON_SRCS)
OBJS = $(SRCS:.c=.o)

libmaelys_datalog.a: $(OBJS)
	ar rcs $@ $^

clean:
	rm -f $(OBJS) libmaelys_datalog.a
