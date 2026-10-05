// libpq_wrapper.c — Thin C glue between Eiwa's runtime and libpq.
//
// The only responsibility here is to adapt types that cannot be expressed
// directly in Eiwa's `lib` blocks:
//   - Converting a collections_List_String to char** for PQexecParams.
//
// All other libpq functions are called directly from Eiwa via @Alias.
//
// Compiled into a real translation unit via `@Source` (the LLVM backend
// ignores @Header includes, so a header alone would never emit these symbols).
//
// LLVM backend layouts (the C backend was removed, 2026-08):
//   - String is a `char*` (no {ptr,length} struct).
//   - `List<String>` is a struct whose first field `items` points to a raw
//     NativeArray buffer: [ i64 size, i64 capacity, i64 elements... ], where
//     each element slot holds a POINTER to a heap String struct {char*, i64}
//     — NO `_type_desc` header. Dereference once to reach the C string.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <alloca.h>

// libpq forward declarations: PGconn/PGresult are opaque to this
// wrapper, so no <libpq-fe.h> is needed to compile. Linking still
// needs -lpq (via @Link("pq")); the toolchain forwards -I/-L/-l
// from CLI flags, and Homebrew keg-only paths work with e.g.
// eiwa build -I/opt/homebrew/opt/libpq/include -L/opt/homebrew/opt/libpq/lib
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;
typedef unsigned int Oid;
PGresult *PQexecParams(PGconn *conn, const char *command, int nparams,
    const Oid *paramTypes, const char *const *paramValues,
    const int *paramLengths, const int *paramFormats, int resultFormat);
PGresult *PQexecPrepared(PGconn *conn, const char *stmtName, int nparams,
    const char *const *paramValues, const int *paramLengths,
    const int *paramFormats, int resultFormat);
PGresult *PQexec(PGconn *conn, const char *command);
int PQgetisnull(const PGresult *res, int row, int col);
char *PQgetvalue(const PGresult *res, int row, int col);
char *PQcmdTuples(PGresult *res);
int PQfnumber(const PGresult *res, const char *field_name);

// NativeArray<String> raw buffer: header of 2 x i64, then `char*` elements.
typedef struct {
    int64_t size;
    int64_t capacity;
    const char* data[0];
} EiwaPqStringArray;

// collections_List_String layout in the LLVM backend: `items` is the first
// (and only relevant) field, at offset 0.
typedef struct {
    EiwaPqStringArray* items;
} EiwaPqListString;

// Execute a parameterized query using a collections_List_String as params.
//
// conn       — PGconn* (opaque to Eiwa)
// command    — null-terminated SQL string (String = char*)
// params_lst — collections_List_String* (may be NULL or have 0 items)
//
// Returns a PGresult* that the caller must pass to PQclear() after reading.
PGresult *eiwa_pq_exec_params(
    PGconn             *conn,
    const char         *command,
    EiwaPqListString   *params_lst)
{
    int nparams = 0;
    const char **param_values = NULL;

    if (params_lst != NULL && params_lst->items != NULL) {
        nparams = (int)params_lst->items->size;
        if (nparams > 0) {
            // Stack-allocate the pointer array — avoids GC pressure for short queries.
            param_values = (const char **)alloca((size_t)nparams * sizeof(char *));
            for (int i = 0; i < nparams; i++) {
                param_values[i] = *(const char **)params_lst->items->data[i];
            }
        }
    }

    return PQexecParams(
        conn,
        command,
        nparams,
        NULL,   // paramTypes — let server infer
        param_values,
        NULL,   // paramLengths — text mode, use strlen
        NULL,   // paramFormats — all text
        0       // resultFormat — text
    );
}

// Execute a prepared statement using a collections_List_String as params.
PGresult *eiwa_pq_exec_prepared(
    PGconn             *conn,
    const char         *stmtName,
    EiwaPqListString   *params_lst)
{
    int nparams = 0;
    const char **param_values = NULL;

    if (params_lst != NULL && params_lst->items != NULL) {
        nparams = (int)params_lst->items->size;
        if (nparams > 0) {
            param_values = (const char **)alloca((size_t)nparams * sizeof(char *));
            for (int i = 0; i < nparams; i++) {
                param_values[i] = *(const char **)params_lst->items->data[i];
            }
        }
    }

    return PQexecPrepared(
        conn,
        stmtName,
        nparams,
        param_values,
        NULL,   // paramLengths — text mode, use strlen
        NULL,   // paramFormats — all text
        0       // resultFormat — text
    );
}

// Convenience: execute a no-param query (wraps PQexec).
PGresult *eiwa_pq_exec(PGconn *conn, const char *command) {
    return PQexec(conn, command);
}

// Get a field value as a null-terminated C string.
// Returns empty string if the value is NULL in the result set.
const char *eiwa_pq_getvalue(PGresult *res, int row, int col) {
    if (PQgetisnull(res, row, col)) return "";
    return PQgetvalue(res, row, col);
}

// Number of rows affected by a non-SELECT command (INSERT, UPDATE, DELETE).
int eiwa_pq_rows_affected(PGresult *res) {
    const char *str = PQcmdTuples(res);
    if (!str || str[0] == '\0') return 0;
    return atoi(str);
}

// Column index by name (-1 if not found).
int eiwa_pq_field_index(PGresult *res, const char *name) {
    return PQfnumber(res, name);
}
