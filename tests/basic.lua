local iconv = require "iconv"

local function assert_error(value, code)
    assert(value == nil, "expected an error")
    if code ~= nil then
        assert(type(code) == "string")
    end
end

local cv, err = iconv.open("UTF-8", "UTF-8")
assert(cv, err and err.message)
assert(cv:is_closed() == false)

local output = assert(cv:convert("hello"))
assert(output == "hello")

local binary = "a\0b"
assert(cv:convert(binary) == binary)
assert(cv:convert("") == "")

local latin1 = assert(iconv.convert("ISO-8859-1", "UTF-8", "\xc3\xa9"))
assert(latin1 == "\xe9")

local one_shot_invalid
one_shot_invalid, err = iconv.convert("UTF-8", "UTF-8", "\xff")
assert_error(one_shot_invalid, "EILSEQ")
assert(err.operation == "convert")

local invalid
invalid, err = cv:convert("\xff")
assert_error(invalid, "EILSEQ")
assert(err.code == "EILSEQ")
assert(err.operation == "convert")
assert(cv:reset())

invalid, err = cv:convert("\xc3")
assert_error(invalid, "EINVAL")
assert(err.code == "EINVAL")
assert(cv:reset())
assert(cv:convert("ok") == "ok")

local unsupported
unsupported, err = iconv.open("X-LICONV-NOT-AN-ENCODING", "UTF-8")
assert_error(unsupported)
assert(err.operation == "open")
assert(type(err.code) == "string")

unsupported, err = iconv.open("UTF-8\0", "UTF-8")
assert_error(unsupported, "EINVAL")
assert(err.operation == "open")

assert(cv:close())
assert(cv:is_closed() == true)
assert(cv:close())
local closed_output
closed_output, err = cv:convert("x")
assert_error(closed_output, "ECANCELED")
assert(err.code == "ECANCELED")
assert(cv:reset() == nil)

do
    local collected = assert(iconv.open("UTF-8", "UTF-8"))
    assert(collected:convert("gc") == "gc")
end
collectgarbage("collect")

print("basic tests passed")
