/* ==============================================================================
 * sindarin-pkg-sqlite/src/sqlite.sn.c — SQLite3 implementation
 * ==============================================================================
 * Implements SqliteDb, SqliteStmt, and SqliteRow via the sqlite3 C API.
 * SqliteRow stores column data in parallel heap arrays freed by elem_release.
 * ============================================================================== */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <sqlite3.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef __sn__SqliteDb   RtSqliteDb;
typedef __sn__SqliteStmt RtSqliteStmt;
typedef __sn__SqliteRow  RtSqliteRow;

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define DB_PTR(d)   ((sqlite3 *)(uintptr_t)(d)->db_ptr)
#define STMT_PTR(s) ((sqlite3_stmt *)(uintptr_t)(s)->stmt_ptr)
#define STMT_DB(s)  ((sqlite3 *)(uintptr_t)(s)->db_ptr)

static void sqlite_check(int rc, sqlite3 *db, const char *ctx)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        fprintf(stderr, "sqlite: %s: %s\n", ctx,
                db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        exit(1);
    }
}

/* ============================================================================
 * Row Building
 * ============================================================================ */

static void cleanup_sqlite_row_elem(void *p)
{
    RtSqliteRow *r = (RtSqliteRow *)p;
    int count = (int)r->col_count;

    char **names  = (char **)(uintptr_t)r->col_names;
    char **values = (char **)(uintptr_t)r->col_values;

    if (names) {
        for (int i = 0; i < count; i++) free(names[i]);
        free(names);
    }
    if (values) {
        for (int i = 0; i < count; i++) free(values[i]);
        free(values);
    }
    free((void *)(uintptr_t)r->col_types);
    free((void *)(uintptr_t)r->col_ints);
    free((void *)(uintptr_t)r->col_floats);
}

static RtSqliteRow build_row(sqlite3_stmt *stmt)
{
    int count = sqlite3_column_count(stmt);
    RtSqliteRow row = {0};
    row.col_count = (long long)count;

    char    **names  = (char **)   calloc((size_t)count, sizeof(char *));
    char    **values = (char **)   calloc((size_t)count, sizeof(char *));
    int      *types  = (int *)     calloc((size_t)count, sizeof(int));
    int64_t  *ints   = (int64_t *) calloc((size_t)count, sizeof(int64_t));
    double   *floats = (double *)  calloc((size_t)count, sizeof(double));

    if (!names || !values || !types || !ints || !floats) {
        fprintf(stderr, "sqlite: build_row: allocation failed\n");
        exit(1);
    }

    for (int i = 0; i < count; i++) {
        const char *col_name = sqlite3_column_name(stmt, i);
        names[i] = strdup(col_name ? col_name : "");
        types[i] = sqlite3_column_type(stmt, i);

        if (types[i] == SQLITE_NULL) {
            values[i] = NULL;
            ints[i]   = 0;
            floats[i] = 0.0;
        } else if (types[i] == SQLITE_INTEGER) {
            ints[i]   = sqlite3_column_int64(stmt, i);
            floats[i] = (double)ints[i];
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)ints[i]);
            values[i] = strdup(buf);
        } else if (types[i] == SQLITE_FLOAT) {
            floats[i] = sqlite3_column_double(stmt, i);
            ints[i]   = (int64_t)floats[i];
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", floats[i]);
            values[i] = strdup(buf);
        } else {
            /* SQLITE_TEXT or SQLITE_BLOB */
            const unsigned char *text = sqlite3_column_text(stmt, i);
            values[i] = strdup(text ? (const char *)text : "");
            ints[i]   = 0;
            floats[i] = 0.0;
        }
    }

    row.col_names  = (long long)(uintptr_t)names;
    row.col_values = (long long)(uintptr_t)values;
    row.col_types  = (long long)(uintptr_t)types;
    row.col_ints   = (long long)(uintptr_t)ints;
    row.col_floats = (long long)(uintptr_t)floats;
    return row;
}

static SnArray *collect_rows(sqlite3_stmt *stmt, sqlite3 *db)
{
    SnArray *arr = sn_array_new(sizeof(RtSqliteRow), 16);
    arr->elem_tag     = SN_TAG_STRUCT;
    arr->elem_release = cleanup_sqlite_row_elem;

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        RtSqliteRow row = build_row(stmt);
        sn_array_push(arr, &row);
    }
    if (rc != SQLITE_DONE)
        sqlite_check(rc, db, "query step");

    return arr;
}

/* ============================================================================
 * SqliteRow Getters
 * ============================================================================ */

static int find_col(RtSqliteRow *row, const char *col)
{
    char **names = (char **)(uintptr_t)row->col_names;
    int count = (int)row->col_count;
    for (int i = 0; i < count; i++) {
        if (names[i] && strcmp(names[i], col) == 0)
            return i;
    }
    return -1;
}

char *sn_sqlite_row_get_string(__sn__SqliteRow *row, char *col)
{
    if (!row || !col) return strdup("");
    int idx = find_col(row, col);
    if (idx < 0) return strdup("");
    char **values = (char **)(uintptr_t)row->col_values;
    return strdup(values[idx] ? values[idx] : "");
}

long long sn_sqlite_row_get_int(__sn__SqliteRow *row, char *col)
{
    if (!row || !col) return 0;
    int idx = find_col(row, col);
    if (idx < 0) return 0;
    int64_t *ints = (int64_t *)(uintptr_t)row->col_ints;
    return (long long)ints[idx];
}

double sn_sqlite_row_get_float(__sn__SqliteRow *row, char *col)
{
    if (!row || !col) return 0.0;
    int idx = find_col(row, col);
    if (idx < 0) return 0.0;
    double *floats = (double *)(uintptr_t)row->col_floats;
    return floats[idx];
}

bool sn_sqlite_row_is_null(__sn__SqliteRow *row, char *col)
{
    if (!row || !col) return true;
    int idx = find_col(row, col);
    if (idx < 0) return true;
    int *types = (int *)(uintptr_t)row->col_types;
    return types[idx] == SQLITE_NULL;
}

long long sn_sqlite_row_column_count(__sn__SqliteRow *row)
{
    if (!row) return 0;
    return row->col_count;
}

char *sn_sqlite_row_column_name(__sn__SqliteRow *row, long long index)
{
    if (!row || index < 0 || index >= row->col_count) return strdup("");
    char **names = (char **)(uintptr_t)row->col_names;
    return strdup(names[index] ? names[index] : "");
}

/* ============================================================================
 * SqliteDb
 * ============================================================================ */

RtSqliteDb *sn_sqlite_db_open(char *path)
{
    if (!path) {
        fprintf(stderr, "SqliteDb.open: path is NULL\n");
        exit(1);
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SqliteDb.open: %s: %s\n", path, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    RtSqliteDb *d = (RtSqliteDb *)calloc(1, sizeof(RtSqliteDb));
    if (!d) {
        fprintf(stderr, "SqliteDb.open: allocation failed\n");
        exit(1);
    }
    d->db_ptr = (long long)(uintptr_t)db;
    return d;
}

void sn_sqlite_db_exec(RtSqliteDb *d, char *sql)
{
    if (!d || !sql) return;
    char *errmsg = NULL;
    int rc = sqlite3_exec(DB_PTR(d), sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SqliteDb.exec: %s\n", errmsg ? errmsg : "unknown error");
        sqlite3_free(errmsg);
        exit(1);
    }
}

SnArray *sn_sqlite_db_query(RtSqliteDb *d, char *sql)
{
    if (!d || !sql) return sn_array_new(sizeof(RtSqliteRow), 0);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(DB_PTR(d), sql, -1, &stmt, NULL);
    sqlite_check(rc, DB_PTR(d), "query prepare");

    SnArray *arr = collect_rows(stmt, DB_PTR(d));
    sqlite3_finalize(stmt);
    return arr;
}

RtSqliteStmt *sn_sqlite_db_prepare(RtSqliteDb *d, char *sql)
{
    if (!d || !sql) {
        fprintf(stderr, "SqliteDb.prepare: NULL argument\n");
        exit(1);
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(DB_PTR(d), sql, -1, &stmt, NULL);
    sqlite_check(rc, DB_PTR(d), "prepare");

    RtSqliteStmt *s = (RtSqliteStmt *)calloc(1, sizeof(RtSqliteStmt));
    if (!s) {
        fprintf(stderr, "SqliteDb.prepare: allocation failed\n");
        exit(1);
    }
    s->stmt_ptr = (long long)(uintptr_t)stmt;
    s->db_ptr   = (long long)(uintptr_t)DB_PTR(d);
    return s;
}

long long sn_sqlite_db_last_insert_id(RtSqliteDb *d)
{
    if (!d) return 0;
    return (long long)sqlite3_last_insert_rowid(DB_PTR(d));
}

long long sn_sqlite_db_changes(RtSqliteDb *d)
{
    if (!d) return 0;
    return (long long)sqlite3_changes(DB_PTR(d));
}

void sn_sqlite_db_dispose(RtSqliteDb *d)
{
    if (!d) return;
    sqlite3_close_v2(DB_PTR(d));
    d->db_ptr = 0;
}

/* ============================================================================
 * SqliteStmt
 * ============================================================================ */

void sn_sqlite_stmt_bind_string(RtSqliteStmt *s, long long index, char *value)
{
    if (!s) return;
    int rc = sqlite3_bind_text(STMT_PTR(s), (int)index, value, -1, SQLITE_TRANSIENT);
    sqlite_check(rc, STMT_DB(s), "bind_string");
}

void sn_sqlite_stmt_bind_int(RtSqliteStmt *s, long long index, long long value)
{
    if (!s) return;
    int rc = sqlite3_bind_int64(STMT_PTR(s), (int)index, (sqlite3_int64)value);
    sqlite_check(rc, STMT_DB(s), "bind_int");
}

void sn_sqlite_stmt_bind_float(RtSqliteStmt *s, long long index, double value)
{
    if (!s) return;
    int rc = sqlite3_bind_double(STMT_PTR(s), (int)index, value);
    sqlite_check(rc, STMT_DB(s), "bind_float");
}

void sn_sqlite_stmt_bind_null(RtSqliteStmt *s, long long index)
{
    if (!s) return;
    int rc = sqlite3_bind_null(STMT_PTR(s), (int)index);
    sqlite_check(rc, STMT_DB(s), "bind_null");
}

void sn_sqlite_stmt_exec(RtSqliteStmt *s)
{
    if (!s) return;
    int rc = sqlite3_step(STMT_PTR(s));
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
        sqlite_check(rc, STMT_DB(s), "stmt exec");
    sqlite3_reset(STMT_PTR(s));
}

SnArray *sn_sqlite_stmt_query(RtSqliteStmt *s)
{
    if (!s) return sn_array_new(sizeof(RtSqliteRow), 0);
    SnArray *arr = collect_rows(STMT_PTR(s), STMT_DB(s));
    sqlite3_reset(STMT_PTR(s));
    return arr;
}

void sn_sqlite_stmt_reset(RtSqliteStmt *s)
{
    if (!s) return;
    sqlite3_reset(STMT_PTR(s));
    sqlite3_clear_bindings(STMT_PTR(s));
}

void sn_sqlite_stmt_dispose(RtSqliteStmt *s)
{
    if (!s) return;
    sqlite3_finalize(STMT_PTR(s));
    s->stmt_ptr = 0;
    s->db_ptr   = 0;
}
