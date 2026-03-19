# sindarin-pkg-sqlite

A SQLite3 client for the [Sindarin](https://github.com/SindarinSDK/sindarin-compiler) programming language. Supports direct SQL execution, row queries with typed accessors, and prepared statements with parameter binding and reuse.

## Installation

Add the package as a dependency in your `sn.yaml`:

```yaml
dependencies:
- name: sindarin-pkg-sqlite
  git: git@github.com:SindarinSDK/sindarin-pkg-sqlite.git
  branch: main
```

Then run `sn --install` to fetch the package.

## Quick Start

```sindarin
import "sindarin-pkg-sqlite/src/sqlite"

fn main(): void =>
    var db: SqliteDb = SqliteDb.openMemory()

    db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INTEGER)")
    db.exec("INSERT INTO users (name, age) VALUES ('Alice', 30)")

    var rows: SqliteRow[] = db.query("SELECT * FROM users")
    print(rows[0].getString("name"))
    print(rows[0].getInt("age"))

    db.dispose()
```

---

## SqliteDb

```sindarin
import "sindarin-pkg-sqlite/src/sqlite"
```

A database connection. Use `:memory:` for an in-memory database, or a file path for a persistent one.

| Method | Signature | Description |
|--------|-----------|-------------|
| `open` | `static fn open(path: str): SqliteDb` | Open a database at the given path |
| `openMemory` | `static fn openMemory(): SqliteDb` | Open an in-memory database |
| `exec` | `fn exec(sql: str): void` | Execute SQL with no results (CREATE, INSERT, UPDATE, DELETE) |
| `query` | `fn query(sql: str): SqliteRow[]` | Execute a SELECT and return all rows |
| `prepare` | `fn prepare(sql: str): SqliteStmt` | Create a prepared statement |
| `lastInsertId` | `fn lastInsertId(): int` | Row ID of the last INSERT |
| `changes` | `fn changes(): int` | Number of rows modified by the last INSERT/UPDATE/DELETE |
| `dispose` | `fn dispose(): void` | Close the database connection |

```sindarin
var db: SqliteDb = SqliteDb.open("/tmp/myapp.db")

db.exec("CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, price REAL)")
db.exec("INSERT INTO items (name, price) VALUES ('widget', 9.99)")

print($"inserted id: {db.lastInsertId()}\n")
print($"rows changed: {db.changes()}\n")

db.dispose()
```

---

## SqliteRow

A single result row. Column values are accessed by name using typed getters.

| Method | Signature | Description |
|--------|-----------|-------------|
| `getString` | `fn getString(col: str): str` | Column value as string (`""` for NULL) |
| `getInt` | `fn getInt(col: str): int` | Column value as integer (`0` for NULL) |
| `getFloat` | `fn getFloat(col: str): double` | Column value as float (`0.0` for NULL) |
| `isNull` | `fn isNull(col: str): bool` | True if the column is SQL NULL |
| `columnCount` | `fn columnCount(): int` | Number of columns in this row |
| `columnName` | `fn columnName(index: int): str` | Column name at the given zero-based index |

```sindarin
var rows: SqliteRow[] = db.query("SELECT name, price, notes FROM items")

for i: int = 0; i < rows.length; i += 1 =>
    print(rows[i].getString("name"))
    print(rows[i].getFloat("price"))
    if rows[i].isNull("notes") =>
        print("no notes\n")
```

---

## SqliteStmt

A prepared statement with parameter binding. Parameters use `?` placeholders and are indexed from 1. Statements can be reset and re-executed with new bindings.

| Method | Signature | Description |
|--------|-----------|-------------|
| `bindString` | `fn bindString(index: int, value: str): void` | Bind a string to the given parameter (1-based) |
| `bindInt` | `fn bindInt(index: int, value: int): void` | Bind an integer to the given parameter (1-based) |
| `bindFloat` | `fn bindFloat(index: int, value: double): void` | Bind a float to the given parameter (1-based) |
| `bindNull` | `fn bindNull(index: int): void` | Bind SQL NULL to the given parameter (1-based) |
| `exec` | `fn exec(): void` | Execute with no results |
| `query` | `fn query(): SqliteRow[]` | Execute and return all result rows |
| `reset` | `fn reset(): void` | Clear bindings for re-use |
| `dispose` | `fn dispose(): void` | Free statement resources |

```sindarin
var stmt: SqliteStmt = db.prepare("INSERT INTO items (name, price) VALUES (?, ?)")

stmt.bindString(1, "gadget")
stmt.bindFloat(2, 24.99)
stmt.exec()

stmt.reset()
stmt.bindString(1, "doohickey")
stmt.bindNull(2)
stmt.exec()

stmt.dispose()
```

Prepared statements can also return rows:

```sindarin
var sel: SqliteStmt = db.prepare("SELECT * FROM items WHERE price < ?")
sel.bindFloat(1, 20.0)
var rows: SqliteRow[] = sel.query()
sel.dispose()
```

---

## Examples

### In-memory database

```sindarin
import "sindarin-pkg-sqlite/src/sqlite"

fn main(): void =>
    var db: SqliteDb = SqliteDb.openMemory()

    db.exec("CREATE TABLE scores (player TEXT, score INTEGER)")
    db.exec("INSERT INTO scores VALUES ('Alice', 100)")
    db.exec("INSERT INTO scores VALUES ('Bob', 85)")

    var rows: SqliteRow[] = db.query("SELECT * FROM scores ORDER BY score DESC")
    for i: int = 0; i < rows.length; i += 1 =>
        print($"{rows[i].getString(\"player\")}: {rows[i].getInt(\"score\")}\n")

    db.dispose()
```

### Bulk insert with prepared statement

```sindarin
import "sindarin-pkg-sqlite/src/sqlite"

fn main(): void =>
    var db: SqliteDb = SqliteDb.open("/tmp/log.db")
    db.exec("CREATE TABLE IF NOT EXISTS log (msg TEXT, level INTEGER)")

    var stmt: SqliteStmt = db.prepare("INSERT INTO log (msg, level) VALUES (?, ?)")

    stmt.bindString(1, "started").bindInt(2, 1).exec().reset()
    stmt.bindString(1, "processing").bindInt(2, 1).exec().reset()
    stmt.bindString(1, "done").bindInt(2, 2).exec()

    stmt.dispose()
    db.dispose()
```

---

## Development

```bash
# Install dependencies (required before make test)
sn --install

make test    # Build and run all tests
make clean   # Remove build artifacts
```

Tests use an in-memory database and require no external services.

## Dependencies

- [sindarin-pkg-libs](https://github.com/SindarinSDK/sindarin-pkg-libs) — provides pre-built `libsqlite3` static libraries for Linux, macOS, and Windows.
- [sindarin-pkg-sdk](https://github.com/SindarinSDK/sindarin-pkg-sdk) — Sindarin standard library.

## License

MIT License
