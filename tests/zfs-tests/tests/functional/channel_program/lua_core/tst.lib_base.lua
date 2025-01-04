--SPDX-License-Identifier: MIT
--[[
--*****************************************************************************
--* Copyright (C) 1994-2016 Lua.org, PUC-Rio.
--*
--* Permission is hereby granted, free of charge, to any person obtaining
--* a copy of this software and associated documentation files (the
--* "Software"), to deal in the Software without restriction, including
--* without limitation the rights to use, copy, modify, merge, publish,
--* distribute, sublicense, and/or sell copies of the Software, and to
--* permit persons to whom the Software is furnished to do so, subject to
--* the following conditions:
--*
--* The above copyright notice and this permission notice shall be
--* included in all copies or substantial portions of the Software.
--*
--* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
--* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
--* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
--* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
--* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
--* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
--* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
--*****************************************************************************
--]]

-- testing metatables

X = 20; B = 30

_ENV = setmetatable({}, {__index=_G})

collectgarbage()

X = X+10
assert(X == 30 and _G.X == 20)
B = false
assert(B == false)
B = nil
assert(B == 30)

assert(getmetatable{} == nil)
assert(getmetatable(4) == nil)
assert(getmetatable(nil) == nil)
a={name = "NAME"}; setmetatable(a, {__metatable = "xuxu",
                    __tostring=function(x) return x.name end})
assert(getmetatable(a) == "xuxu")
assert(tostring(a) == "NAME")

local a, t = {10,20,30; x="10", y="20"}, {}
assert(setmetatable(a,t) == a)
assert(getmetatable(a) == t)
assert(setmetatable(a,nil) == a)
assert(getmetatable(a) == nil)
assert(setmetatable(a,t) == a)


function f (t, i, e)
  assert(not e)
  local p = rawget(t, "parent")
  return (p and p[i]+3), "dummy return"
end

t.__index = f

a.parent = {z=25, x=12, [4] = 24}
assert(a[1] == 10 and a.z == 28 and a[4] == 27 and a.x == "10")

collectgarbage()

a = setmetatable({}, t)
function f(t, i, v) rawset(t, i, v-3) end
setmetatable(t, t)   -- causes a bug in 5.1 !
t.__newindex = f
a[1] = 30; a.x = "101"; a[5] = 200
assert(a[1] == 27 and a.x == 98 and a[5] == 197)


local c = {}
a = setmetatable({}, t)
t.__newindex = c
a[1] = 10; a[2] = 20; a[3] = 90
assert(c[1] == 10 and c[2] == 20 and c[3] == 90)


do
  local a;
  a = setmetatable({}, {__index = setmetatable({},
                     {__index = setmetatable({},
                     {__index = function (_,n) return a[n-3]+4, "lixo" end})})})
  a[0] = 20
  for i=0,10 do
    assert(a[i*3] == 20 + i*4)
  end
end


do  -- newindex
  local foi
  local a = {}
  for i=1,10 do a[i] = 0; a['a'..i] = 0; end
  setmetatable(a, {__newindex = function (t,k,v) foi=true; rawset(t,k,v) end})
  foi = false; a[1]=0; assert(not foi)
  foi = false; a['a1']=0; assert(not foi)
  foi = false; a['a11']=0; assert(foi)
  foi = false; a[11]=0; assert(foi)
  foi = false; a[1]=nil; assert(not foi)
  foi = false; a[1]=nil; assert(foi)
end


setmetatable(t, nil)
function f (t, ...) return t, {...} end
t.__call = f

do
  local x,y = a(table.unpack{'a', 1})
  assert(x==a and y[1]=='a' and y[2]==1 and y[3]==nil)
  x,y = a()
  assert(x==a and y[1]==nil)
end


local b = setmetatable({}, t)
setmetatable(b,t)

function f(op)
  return function (...) cap = {[0] = op, ...} ; return (...) end
end
t.__add = f("add")
t.__sub = f("sub")
t.__mul = f("mul")
t.__div = f("div")
t.__mod = f("mod")
t.__unm = f("unm")
t.__pow = f("pow")
t.__len = f("len")

assert(b+5 == b)
assert(cap[0] == "add" and cap[1] == b and cap[2] == 5 and cap[3]==nil)
assert(b+'5' == b)
assert(cap[0] == "add" and cap[1] == b and cap[2] == '5' and cap[3]==nil)
assert(5+b == 5)
assert(cap[0] == "add" and cap[1] == 5 and cap[2] == b and cap[3]==nil)
assert('5'+b == '5')
assert(cap[0] == "add" and cap[1] == '5' and cap[2] == b and cap[3]==nil)
b=b-3; assert(getmetatable(b) == t)
assert(5-a == 5)
assert(cap[0] == "sub" and cap[1] == 5 and cap[2] == a and cap[3]==nil)
assert('5'-a == '5')
assert(cap[0] == "sub" and cap[1] == '5' and cap[2] == a and cap[3]==nil)
assert(a*a == a)
assert(cap[0] == "mul" and cap[1] == a and cap[2] == a and cap[3]==nil)
assert(a/0 == a)
assert(cap[0] == "div" and cap[1] == a and cap[2] == 0 and cap[3]==nil)
assert(a%2 == a)
assert(cap[0] == "mod" and cap[1] == a and cap[2] == 2 and cap[3]==nil)
assert(-a == a)
assert(cap[0] == "unm" and cap[1] == a)
assert(a^4 == a)
assert(cap[0] == "pow" and cap[1] == a and cap[2] == 4 and cap[3]==nil)
assert(a^'4' == a)
assert(cap[0] == "pow" and cap[1] == a and cap[2] == '4' and cap[3]==nil)
assert(4^a == 4)
assert(cap[0] == "pow" and cap[1] == 4 and cap[2] == a and cap[3]==nil)
assert('4'^a == '4')
assert(cap[0] == "pow" and cap[1] == '4' and cap[2] == a and cap[3]==nil)
assert(#a == a)
assert(cap[0] == "len" and cap[1] == a)


-- test for rawlen
t = setmetatable({1,2,3}, {__len = function () return 10 end})
assert(#t == 10 and rawlen(t) == 3)
assert(rawlen"abc" == 3)
assert(rawlen(string.rep('a', 1000)) == 1000)

t = {}
t.__lt = function (a,b,c)
  collectgarbage()
  assert(c == nil)
  if type(a) == 'table' then a = a.x end
  if type(b) == 'table' then b = b.x end
 return a<b, "dummy"
end

function Op(x) return setmetatable({x=x}, t) end

local function test ()
  assert(not(Op(1)<Op(1)) and (Op(1)<Op(2)) and not(Op(2)<Op(1)))
  assert(not(1 < Op(1)) and (Op(1) < 2) and not(2 < Op(1)))
  assert(not(Op('a')<Op('a')) and (Op('a')<Op('b')) and not(Op('b')<Op('a')))
  assert(not('a' < Op('a')) and (Op('a') < 'b') and not(Op('b') < Op('a')))
  assert((Op(1)<=Op(1)) and (Op(1)<=Op(2)) and not(Op(2)<=Op(1)))
  assert((Op('a')<=Op('a')) and (Op('a')<=Op('b')) and not(Op('b')<=Op('a')))
  assert(not(Op(1)>Op(1)) and not(Op(1)>Op(2)) and (Op(2)>Op(1)))
  assert(not(Op('a')>Op('a')) and not(Op('a')>Op('b')) and (Op('b')>Op('a')))
  assert((Op(1)>=Op(1)) and not(Op(1)>=Op(2)) and (Op(2)>=Op(1)))
  assert((1 >= Op(1)) and not(1 >= Op(2)) and (Op(2) >= 1))
  assert((Op('a')>=Op('a')) and not(Op('a')>=Op('b')) and (Op('b')>=Op('a')))
  assert(('a' >= Op('a')) and not(Op('a') >= 'b') and (Op('b') >= Op('a')))
end

test()

t.__le = function (a,b,c)
  assert(c == nil)
  if type(a) == 'table' then a = a.x end
  if type(b) == 'table' then b = b.x end
 return a<=b, "dummy"
end

test()  -- retest comparisons, now using both `lt' and `le'


-- test `partial order'

local function Set(x)
  local y = {}
  for _,k in pairs(x) do y[k] = 1 end
  return setmetatable(y, t)
end

t.__lt = function (a,b)
  for k in pairs(a) do
    if not b[k] then return false end
    b[k] = nil
  end
  return next(b) ~= nil
end

t.__le = nil

assert(Set{1,2,3} < Set{1,2,3,4})
assert(not(Set{1,2,3,4} < Set{1,2,3,4}))
assert((Set{1,2,3,4} <= Set{1,2,3,4}))
assert((Set{1,2,3,4} >= Set{1,2,3,4}))
assert((Set{1,3} <= Set{3,5}))   -- wrong!! model needs a `le' method ;-)

t.__le = function (a,b)
  for k in pairs(a) do
    if not b[k] then return false end
  end
  return true
end

assert(not (Set{1,3} <= Set{3,5}))   -- now its OK!
assert(not(Set{1,3} <= Set{3,5}))
assert(not(Set{1,3} >= Set{3,5}))

t.__eq = function (a,b)
  for k in pairs(a) do
    if not b[k] then return false end
    b[k] = nil
  end
  return next(b) == nil
end

local s = Set{1,3,5}
assert(s == Set{3,5,1})
assert(not rawequal(s, Set{3,5,1}))
assert(rawequal(s, s))
assert(Set{1,3,5,1} == Set{3,5,1})
assert(Set{1,3,5} ~= Set{3,5,1,6})
t[Set{1,3,5}] = 1
assert(t[Set{1,3,5}] == nil)   -- `__eq' is not valid for table accesses


t.__concat = function (a,b,c)
  assert(c == nil)
  if type(a) == 'table' then a = a.val end
  if type(b) == 'table' then b = b.val end
  if A then return a..b
  else
    return setmetatable({val=a..b}, t)
  end
end

c = {val="c"}; setmetatable(c, t)
d = {val="d"}; setmetatable(d, t)

A = true
assert(c..d == 'cd')
assert(0 .."a".."b"..c..d.."e".."f"..(5+3).."g" == "0abcdef8g")

A = false
assert((c..d..c..d).val == 'cdcd')
x = c..d
assert(getmetatable(x) == t and x.val == 'cd')
x = 0 .."a".."b"..c..d.."e".."f".."g"
assert(x.val == "0abcdefg")


-- concat metamethod x numbers (bug in 5.1.1)
c = {}
local x
setmetatable(c, {__concat = function (a,b)
  assert(type(a) == "number" and b == c or type(b) == "number" and a == c)
  return c
end})
assert(c..5 == c and 5 .. c == c)
assert(4 .. c .. 5 == c and 4 .. 5 .. 6 .. 7 .. c == c)


-- test comparison compatibilities
local t1, t2, c, d
t1 = {};  c = {}; setmetatable(c, t1)
d = {}
t1.__eq = function () return true end
t1.__lt = function () return true end
setmetatable(d, t1)
assert(c == d and c < d and not(d <= c))
t2 = {}
t2.__eq = t1.__eq
t2.__lt = t1.__lt
setmetatable(d, t2)
assert(c == d and c < d and not(d <= c))



-- test for several levels of calls
local i
local tt = {
  __call = function (t, ...)
    i = i+1
    if t.f then return t.f(...)
    else return {...}
    end
  end
}

local a = setmetatable({}, tt)
local b = setmetatable({f=a}, tt)
local c = setmetatable({f=b}, tt)

i = 0
x = c(3,4,5)
assert(i == 3 and x[1] == 3 and x[3] == 5)


assert(_G.X == 20)


local _g = _G
_ENV = setmetatable({}, {__index=function (_,k) return _g[k] end})


a = {}
rawset(a, "x", 1, 2, 3)
assert(a.x == 1 and rawget(a, "x", 3) == 1)


-- bug in 5.1
T, K, V = nil
grandparent = {}
grandparent.__newindex = function(t,k,v) T=t; K=k; V=v end

parent = {}
parent.__newindex = parent
setmetatable(parent, grandparent)

child = setmetatable({}, parent)
child.foo = 10      --> CRASH (on some machines)
assert(T == parent and K == "foo" and V == 10)


-- testing 'tonumber'
assert(tonumber{} == nil)
assert(tonumber('-012') == -010-2)
assert(tonumber("0xffffffffffff") == 2^(4*12) - 1)
assert(tonumber("0x"..string.rep("f", 150)) == 2^(4*150) - 1)

-- testing 'tonumber' with base
assert(tonumber('  001010  ', 2) == 10)
assert(tonumber('  001010  ', 10) == 1010)
assert(tonumber('  -1010  ', 2) == -10)
assert(tonumber('10', 36) == 36)
assert(tonumber('  -10  ', 36) == -36)
assert(tonumber('  +1Z  ', 36) == 36 + 35)
assert(tonumber('  -1z  ', 36) == -36 + -35)
assert(tonumber('-fFfa', 16) == -(10+(16*(15+(16*(15+(16*15)))))))
assert(tonumber(string.rep('1', 42), 2) + 1 == 2^42)
assert(tonumber(string.rep('1', 34), 2) + 1 == 2^34)
assert(tonumber('ffffFFFF', 16)+1 == 2^32)
assert(tonumber('0ffffFFFF', 16)+1 == 2^32)
assert(tonumber('-0ffffffFFFF', 16) - 1 == -2^40)
for i = 2,36 do
  assert(tonumber('\t10000000000\t', i) == i^10)
end

-- testing 'tonumber' for invalid formats
function f(...)
  if select('#', ...) == 1 then
    return (...)
  else
    return "***"
  end
end

assert(f(tonumber('fFfa', 15)) == nil)
assert(f(tonumber('099', 8)) == nil)
assert(f(tonumber('1\0', 2)) == nil)
assert(f(tonumber('', 8)) == nil)
assert(f(tonumber('  ', 9)) == nil)
assert(f(tonumber('0xf', 10)) == nil)

assert(f(tonumber('inf')) == nil)
assert(f(tonumber(' INF ')) == nil)
assert(f(tonumber('Nan')) == nil)
assert(f(tonumber('nan')) == nil)

assert(f(tonumber('')) == nil)
assert(f(tonumber('1  a')) == nil)
assert(f(tonumber('1\0')) == nil)
assert(f(tonumber('1 \0')) == nil)
assert(f(tonumber('1\0 ')) == nil)
assert(f(tonumber('e1')) == nil)
assert(f(tonumber('e  1')) == nil)


-- testing 'tonumber' for invalid hexadecimal formats
assert(tonumber('0x') == nil)
assert(tonumber('x') == nil)
assert(tonumber('x3') == nil)
assert(tonumber('00x2') == nil)
assert(tonumber('0x 2') == nil)
assert(tonumber('0 x2') == nil)
assert(tonumber('23x') == nil)
assert(tonumber('- 0xaa') == nil)


-- testing hexadecimal numerals
assert(tonumber('+0x2') == 2)
assert(tonumber('-0xaA') == -170)
assert(tonumber('-0xffFFFfff') == -2^32 + 1)


-- testing 'tostring'
assert(tostring("alo") == "alo")
assert(tostring(12) == "12")
assert(tostring(1234567890123) == '1234567890123')
assert(type(tostring("hello")) == "string")
assert(tostring(true) == "true")
assert(tostring(false) == "false")
assert(string.find(tostring{}, 'table:'))
assert(string.find(tostring(select), 'function:'))
assert(#tostring('\0') == 1)


-- testing ipairs
local x = 0
for k,v in ipairs{10,20,30;x=12} do
  x = x + 1
  assert(k == x and v == x * 10)
end

for _ in ipairs{x=12, y=24} do assert(nil) end

-- test for 'false' x ipair
x = false
local i = 0
for k,v in ipairs{true,false,true,false} do
  i = i + 1
  x = not x
  assert(x == v)
end
assert(i == 4)


return "OK"
