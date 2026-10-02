# Local patches to Lua 5.0.2 (pristine tarball sha256 a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc)

Game Wave scripts use Lua 5.0.2 built with an integer `lua_Number` (int32). Configuration
lives in `src/luauser.h` of Undertow (LUA_USER_H). Source changes, all marked `UNDERTOW`:

- `src/lvm.c` (Arith, TM_DIV): integer division by zero yields 0 instead of trapping the host.
- `src/lib/lstrlib.c` (str_format %e/%f/%g): cast the integer argument to double.
- `src/lundump.c` (LoadSize, LoadHeader): bytecode `size_t` is always 32-bit (64-bit hosts).
