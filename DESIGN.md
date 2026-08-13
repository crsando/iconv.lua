# Lua/LuaJIT `iconv` 库设计

状态：待评审。本文只定义第一版方案，暂不包含源码实现。

## 1. 第一版范围

第一版只支持：

- Lua 5.1；
- LuaJIT 2.0/2.1（使用 Lua 5.1 兼容 API）；
- Linux；
- macOS；
- 系统提供的 POSIX `iconv(3)`，必要时允许链接 GNU libiconv。

第一版暂不支持 Lua 5.2/5.3/5.4、Windows、其他 Unix 平台、CMake、LuaRocks 和运行时动态加载后端。

目标是提供简单、二进制安全的完整字符串转换。输入和输出都是 Lua 字符串，不做编码检测、Unicode 规范化或 code point 拆分。

为控制首版复杂度，不实现分块流式 API。`feed`、`finish`、转换统计和输出大小限制可以在后续版本增加。

## 2. 总体方案

模块名为 `iconv`，动态模块文件名为 `iconv.so`。Linux 和 macOS 都使用这个文件名，便于通过 `package.cpath` 加载。

Lua 层只做三件事：

1. 检查参数并调用 C API；
2. 用 userdata 持有 `iconv_t`；
3. 把 C 错误转换成 Lua table。

C 层直接调用：

- `iconv_open(to, from)`；
- `iconv(cd, ...)`；
- `iconv(cd, NULL, NULL, ...)` 重置状态；
- `iconv_close(cd)`。

编码名称和转换方向遵循 `iconv_open`：`to` 是目标编码，`from` 是源编码。

```lua
local iconv = require "iconv"
local cv, err = iconv.open("UTF-8", "GB18030")
local output, convert_err = cv:convert(input)
```

默认严格转换。需要后端支持的特殊行为时，调用方可以直接把后缀写进编码名，例如 `"ASCII//TRANSLIT"` 或 `"ASCII//IGNORE"`。库本身不模拟这些后缀，也不默认启用它们。

## 3. Lua API

### 3.1 `iconv.open`

```lua
local cv, err = iconv.open(to, from)
```

成功返回转换器 userdata；失败返回 `nil, err`。

参数必须是 Lua 字符串。编码名中不能包含 NUL 字节，因为后端 API 使用以 NUL 结尾的 C 字符串。编码名为空或后端不支持时，由 `iconv_open` 返回错误。

### 3.2 `iconv.convert`

```lua
local output, err = iconv.convert(to, from, input)
```

这是便捷函数，内部执行 `open`、一次 `cv:convert` 和 `close`。适合短字符串；大数据调用方仍可以复用一个转换器，减少反复创建句柄的开销。

### 3.3 `cv:convert`

```lua
local output, err = cv:convert(input)
```

对完整的 Lua 字符串执行一次转换，并返回新的 Lua 字符串。输入长度通过 Lua API 的显式长度获得，因此支持 NUL 字节和空字符串。

每次调用前，库把转换描述符重置到初始状态；转换完成后再执行一次 reset，确保状态型编码不会影响下一次调用。该方法不保存输入，也不依赖调用方按字符边界切分数据。

如果输入以不完整的多字节序列结束，转换失败并返回 `EINVAL`；不把尾部留到下一次调用。下一次 `convert` 会再次自动重置描述符；调用方也可以通过 `cv:reset()` 显式重置。

### 3.4 `cv:reset`

```lua
local ok, err = cv:reset()
```

把转换器恢复到初始状态。成功返回 `true`；失败返回 `nil, err`。第一版没有流式状态，但保留该方法用于显式恢复后端状态和为后续扩展留出 API。

### 3.5 `cv:close`

```lua
local ok, err = cv:close()
```

关闭 `iconv_t` 并释放资源。操作幂等，重复关闭返回 `true`。Lua GC 回收 userdata 时也会自动关闭；`__gc` 不抛出 Lua 异常。

### 3.6 `cv:is_closed`

```lua
if cv:is_closed() then ... end
```

返回布尔值，便于宿主代码检查显式关闭后的对象。

## 4. 错误约定

输入数据错误、后端错误和资源错误使用 `nil, error` 返回，不调用 `lua_error`。参数类型错误、缺少参数等编程错误使用 Lua 5.1 标准的 `luaL_check*` 机制抛出。

错误 table 结构：

```lua
{
    code = "EILSEQ",       -- 稳定的错误分类
    message = "...",       -- 人类可读信息
    errno = 84,             -- 系统 errno；没有时为 nil
    operation = "convert"  -- open/convert/reset/close
}
```

错误分类至少包含：

| 系统错误 | `code` | 含义 |
| --- | --- | --- |
| `EILSEQ` | `EILSEQ` | 输入序列非法，或目标编码无法表示输入 |
| `EINVAL` | `EINVAL` | 输入以不完整序列结束 |
| `E2BIG` | `E2BIG` | 当前输出缓冲区不足；C 层应自动扩容，只有无法继续时才返回 |
| `ENOMEM` | `ENOMEM` | 分配输出缓冲或 userdata 失败 |
| 已关闭对象 | `ECANCELED` | 对已关闭转换器执行操作 |
| 其他 | 对应 errno 名称 | 保留后端错误 |

`message` 只用于显示，不作为程序逻辑判断依据。不同系统的 `iconv` 对不可表示字符、别名和 `//TRANSLIT` 的具体行为可能不同；第一版只保证错误结构和调用约定一致，不保证跨系统输出逐字节相同。

## 5. C 对象和转换流程

每个转换器 userdata 保存：

- `iconv_t cd`；
- `closed` 标志；
- `to`、`from` 的副本（仅用于诊断，可选）；
- Lua 5.1 userdata 的元表引用。

核心流程：

1. `open` 检查编码名长度，调用 `iconv_open`，失败时读取 `errno` 并返回错误 table。
2. `convert` 调用 reset，把输入指针和剩余长度交给 `iconv`。
3. C 层使用 `luaL_Buffer` 和固定大小临时输出块循环调用 `iconv`；`E2BIG` 表示临时块已满时继续追加，不把正常扩容误报给调用方。
4. 输入全部消费后调用 reset 形式的 `iconv`，追加状态切换产生的输出。
5. 完成 Lua 字符串并返回；若任一步失败，返回错误 table。下一次转换开始时会再次执行 reset。
6. `close` 和 `__gc` 调用 `iconv_close`，清空句柄并设置 `closed`。

Lua 字符串不会被修改。C 层始终使用显式长度，不能使用 `strlen` 处理输入或输出。

## 6. Lua 5.1/LuaJIT 兼容策略

第一版只针对 Lua 5.1 API 编译，不创建多版本兼容层。LuaJIT 直接使用其 Lua 5.1 兼容头文件和 ABI。

代码只依赖以下公共 API：

- `luaL_checklstring`、`luaL_optstring`；
- `lua_newuserdata`、`luaL_newmetatable`、`luaL_checkudata`；
- `luaL_register` 或等价的 Lua 5.1 注册方式；
- `luaL_Buffer` 相关函数；
- `lua_pushlstring`、`lua_pushboolean`、`lua_pushstring`。

不使用 Lua 5.2 以后新增的 API，不使用 LuaJIT 私有扩展。后续若要支持其他 Lua 版本，再单独增加兼容头和条件编译。

## 7. Linux/macOS 后端策略

- Linux：默认从系统 `<iconv.h>` 和 libc 链接；某些发行版也可以通过 `ICONV_LIBS=-liconv` 使用 GNU libiconv。
- macOS：默认使用系统 `<iconv.h>` 和 libc；如果使用 Homebrew/MacPorts 的 GNU libiconv，可通过 `ICONV_INC`、`ICONV_LIBS` 覆盖。
- 不在运行时 `dlopen` 库，后端在编译和链接时确定。
- 第一版不承诺 Windows、FreeBSD、Android 或其他平台的构建结果。

## 8. Makefile 设计

项目采用单一 Makefile，不使用 CMake。建议目录：

```text
src/iconv.c       # Lua 模块、userdata 和 iconv 调用
tests/basic.lua   # 基础测试
examples/*.lua    # 使用示例
Makefile
DESIGN.md
```

建议变量：

| 变量 | 默认/用途 |
| --- | --- |
| `CC` | `cc` |
| `CFLAGS` | `-O2 -fPIC` |
| `LUA_INC` | Lua 5.1 或 LuaJIT 头文件目录，由用户按环境覆盖 |
| `LUA_BIN` | `lua`，测试 LuaJIT 时设置为 `luajit` |
| `ICONV_INC` | 可选的 iconv 头文件目录 |
| `ICONV_LIBS` | 默认空；需要独立 libiconv 时设为 `-liconv` |
| `PREFIX` | 安装前缀，默认 `/usr/local` |

目标：

```text
make                    # 编译 iconv.so
make test LUA_BIN=luajit
make install PREFIX=...
make clean
```

Linux 使用 `-shared` 链接模块；macOS 使用 `-bundle -undefined dynamic_lookup`。两者产物都命名为 `iconv.so`。模块不直接链接 Lua 库，Lua 符号由宿主解释器提供；如果某个发行版要求显式链接，可通过 `LUA_LIBS` 覆盖。

安装目标把 `iconv.so` 放入 `$(PREFIX)/lib/lua/5.1/`，并允许通过 `LUA_CPATH` 或 `package.cpath` 使用自定义目录。Makefile 不自动修改用户的 Lua 配置。

## 9. 测试范围

第一版测试使用 Lua 5.1 或 LuaJIT 执行同一组脚本：

1. ASCII、UTF-8、Latin-1 之间的基本转换；
2. 空字符串、包含 NUL 的字符串和较长字符串；
3. 非法 UTF-8 输入和不完整多字节输入，验证 `EILSEQ`/`EINVAL`；
4. 不支持的编码名，验证 `open` 返回错误而不是崩溃；
5. 重复 `convert`、`reset`、`close` 和 GC 生命周期；
6. 已关闭对象调用方法，验证 `ECANCELED`；
7. Linux 和 macOS 的 `make`、`make test`。

测试只断言 API 返回值、错误分类和资源行为。对于 `//TRANSLIT`、编码别名和具体替换字符，只在当前后端明确支持时测试，不把某个系统的特殊输出当作跨平台契约。

## 10. 后续扩展（不属于第一版）

- `feed`/`finish` 流式转换和不完整尾部缓存；
- 输出大小、临时缓存大小和内存使用上限；
- 转换统计信息；
- Lua 5.2-5.4 兼容层；
- Windows 和其他 Unix 平台；
- LuaRocks 打包、CMake 构建和 CI 矩阵；
- 更丰富的后端能力查询。

## 11. 第一版确认项

开始实现前只需要确认以下事项：

1. 模块名是否采用 `iconv`（推荐；仓库名仍为 `liconv`）；
2. 是否接受第一版只提供完整字符串转换，不提供 `feed`/`finish`；
3. 是否接受 `nil, error` 的运行时错误和 Lua 参数异常的混合风格（推荐）；
4. 是否采用 MIT 许可证（当前仓库尚未提供许可证文件，推荐）。

确认后即可按本文实现 `src/iconv.c`、`Makefile`、测试和示例。
