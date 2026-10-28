# iconv.lua

`iconv.lua` 是一个面向 Lua 5.1 和 LuaJIT 的轻量级字符编码转换模块。它把系统提供的 POSIX `iconv(3)` 封装成简单的 Lua API，输入和输出都是 Lua 字符串。

当前版本支持 Linux 和 macOS，使用系统 `iconv`（也可以在构建时指定 GNU libiconv）。模块执行完整字符串转换，不做编码检测、Unicode 规范化或 code point 拆分。

## 特性

- 支持 Lua 5.1 和 LuaJIT 2.0/2.1；
- 支持 UTF-8、GB18030、Latin-1 等由系统 `iconv` 提供的编码；
- 二进制安全，支持空字符串和包含 NUL 字节的输入；
- 可复用转换器，避免重复创建 `iconv_t`；
- 运行时错误以 `nil, error` 返回，参数类型错误遵循 Lua 标准异常；
- 转换器由显式 `close` 或 Lua 垃圾回收自动释放。

## 环境要求

- Linux 或 macOS；
- C 编译器（默认使用 `cc`）；
- Lua 5.1 或 LuaJIT 的头文件；
- 系统 POSIX `iconv`。若使用独立的 GNU libiconv，可在构建时传入 `ICONV_INC` 和 `ICONV_LIBS`。

第一版暂不提供 Windows、其他 Unix 平台、Lua 5.2+、LuaRocks 或 CMake 支持。

## 构建

在项目根目录执行：

```sh
make
```

构建结果为根目录下的 `iconv.so`。如果默认路径找不到 Lua 头文件，可以手动指定头文件目录：

```sh
make LUA_INC=/usr/local/include/luajit-2.1
```

macOS 不需要额外参数：`iconv(3)` 在 `libiconv.dylib` 里而不在 `libSystem`，Makefile 在
Darwin 上会自动加上 `-liconv`。不链接的话模块能编出来，但 `require "iconv"` 时报
`symbol not found in flat namespace '_iconv'`。

明确使用 Homebrew 的 GNU libiconv（而不是系统那份）时：

```sh
make ICONV_INC=/opt/homebrew/include ICONV_LIBS="-L/opt/homebrew/lib -liconv"
```

命令行给出的 `ICONV_LIBS` 会覆盖默认值（包括传空值 `ICONV_LIBS=` 关掉它）。

Makefile 支持的常用变量：

| 变量 | 用途 |
| --- | --- |
| `CC` | C 编译器，默认 `cc` |
| `CFLAGS` | 编译选项，默认 `-O2 -fPIC` |
| `LUA_INC` | Lua 5.1/LuaJIT 头文件目录 |
| `LUA_BIN` | 测试使用的解释器，默认 `luajit` |
| `ICONV_INC` | 可选的 iconv 头文件目录 |
| `ICONV_LIBS` | 可选的 iconv 链接参数，例如 `-liconv` |
| `LUA_LIBS` | 必要时覆盖 Lua 链接参数 |
| `PREFIX` | 安装前缀，默认取 PATH 里 `luajit` 所在前缀（Homebrew 为 `/opt/homebrew`，找不到时 `/usr/local`） |

## 测试

使用默认解释器运行测试：

```sh
make test
```

也可以显式选择 LuaJIT 或 Lua 5.1：

```sh
make test LUA_BIN=luajit
make test LUA_BIN=lua5.1
```

测试会临时把项目目录加入 `LUA_CPATH`，不修改用户的 Lua 配置。

## 安装

```sh
make install                 # 装到 luajit 所在前缀
make install PREFIX=/usr/local  # 或显式指定
```

模块会安装到 `$(PREFIX)/lib/lua/5.1/iconv.so`。如果使用自定义目录，设置 `LUA_CPATH` 或在 Lua 中调整 `package.cpath`：

```sh
LUA_CPATH="/usr/local/lib/lua/5.1/?.so;;" luajit your_program.lua
```

## 快速开始

`iconv.open` 的参数顺序与 `iconv_open` 一致：第一个参数是目标编码，第二个参数是源编码。

```lua
local iconv = require "iconv"

-- 一次性转换，适合短字符串
local output, err = iconv.convert("UTF-8", "GB18030", "\xe4\xbd\xa0\xe5\xa5\xbd")
if not output then
    error(err.message)
end
print(output) -- 你好

-- 复用转换器，适合多次转换
local converter = assert(iconv.open("UTF-8", "ISO-8859-1"))
local text = assert(converter:convert("caf\xe9"))
print(text)
assert(converter:close())
```

完整示例见 [`examples/basic.lua`](examples/basic.lua)。

## API

### `iconv.open(to, from)`

打开一个转换器。成功返回转换器 userdata；失败返回 `nil, error`。

编码名必须是 Lua 字符串，且不能包含 NUL 字节。编码是否可用由系统 `iconv` 后端决定。

```lua
local converter, err = iconv.open("UTF-8", "GB18030")
```

### `iconv.convert(to, from, input)`

便捷的一次性转换函数，内部执行 `open`、一次 `converter:convert` 和 `close`。适合短字符串；需要重复转换时应复用 `iconv.open` 返回的转换器。

### `converter:convert(input)`

转换完整的 Lua 字符串并返回新字符串。输入长度通过 Lua API 获取，因此 NUL 字节不会截断输入，空字符串也可以正常转换。

每次转换前描述符都会重置到初始状态，转换结束时会处理状态型编码的收尾输出。输入末尾是不完整的多字节序列时，转换失败并返回 `EINVAL`，不会把尾部保存到下一次调用。

### `converter:reset()`

将转换器恢复到初始状态。成功返回 `true`；失败返回 `nil, error`。对已关闭的转换器调用会返回 `ECANCELED`。

### `converter:close()`

关闭转换器并释放底层资源。操作幂等，重复关闭仍返回 `true`。垃圾回收时也会自动关闭。

### `converter:is_closed()`

返回布尔值，表示转换器是否已经关闭。

## 错误处理

转换、打开、重置和关闭失败都返回 `nil, error`，不会通过 `lua_error` 抛出运行时错误。错误 table 的结构如下：

```lua
{
    code = "EILSEQ",       -- 稳定的错误分类
    message = "...",       -- 人类可读信息
    errno = 84,             -- 系统 errno；没有时可能为 nil
    operation = "convert"  -- open/convert/reset/close
}
```

常见错误分类：

| `code` | 含义 |
| --- | --- |
| `EILSEQ` | 输入序列非法，或目标编码无法表示输入 |
| `EINVAL` | 输入以不完整序列结束，或编码名包含 NUL |
| `E2BIG` | 输出缓冲区无法继续扩容 |
| `ENOMEM` | 分配输出缓冲或 userdata 失败 |
| `ECANCELED` | 对已关闭转换器执行操作 |
| 其他 errno 名称 | 后端返回的其他系统错误 |

`message` 只用于展示，不应作为程序逻辑判断依据。不同系统对编码别名、不可表示字符和特殊后缀的行为可能不同。

## 后缀与转换语义

库默认执行严格转换。需要后端支持的行为时，可以直接把后缀写进编码名，例如：

```lua
local output, err = iconv.convert("ASCII//TRANSLIT", "UTF-8", input)
```

库不会自行模拟 `//TRANSLIT` 或 `//IGNORE`，具体结果取决于当前系统的 `iconv` 实现。

## 限制与后续计划

当前版本没有 `feed`/`finish` 流式 API，不缓存不完整输入，也没有输出大小限制、转换统计或运行时后端切换。后续可考虑增加流式转换、Lua 5.2-5.4 兼容层、Windows/其他 Unix 支持，以及 LuaRocks、CMake 和 CI 配置。

## 许可证

本仓库当前未包含许可证文件；发布或集成前请根据项目维护者的决定补充许可证信息。
