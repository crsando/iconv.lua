local iconv = require "iconv"

local input = "\xe4\xbd\xa0\xe5\xa5\xbd"
local output, err = iconv.convert("UTF-8", "GB18030", input)
assert(output, err and err.message)
print(output)
