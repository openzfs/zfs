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

-- testing table library

-- workaround missing pcall in zfs lua implementation
local function tuple(...)
  return {n=select('#', ...), ...}
end

function pcall(f, ...)
  local co = coroutine.create(f)
  local res = tuple(coroutine.resume(co, ...))
  if res[1] and coroutine.status(co) == "suspended" then
    res[1] = false
  end
  return table.unpack(res, 1, res.n)
end


-- workaround missing math lib in zfs lua implementation
local A1, A2 = 727595, 798405  -- 5^17=D20*A1+A2
local D20, D40 = 1048576, 1099511627776  -- 2^20, 2^40
local X1, X2 = 0, 1
function rand()
    local U = X2*A2
    local V = (X1*A2 + X2*A1) % D20
    V = (V*D20 + U) % D40
    X1 = V/D20
    X2 = V - X1*D20
    return V*100/D40
end


-- testing unpack

local unpack = table.unpack

local x,y,z,a,n
a = {}; lim = 2000
for i=1, lim do a[i]=i end
assert(select(lim, unpack(a)) == lim and select('#', unpack(a)) == lim)
x = unpack(a)
assert(x == 1)
x = {unpack(a)}
assert(#x == lim and x[1] == 1 and x[lim] == lim)
x = {unpack(a, lim-2)}
assert(#x == 3 and x[1] == lim-2 and x[3] == lim)
x = {unpack(a, 10, 6)}
assert(next(x) == nil)   -- no elements
x = {unpack(a, 11, 10)}
assert(next(x) == nil)   -- no elements
x,y = unpack(a, 10, 10)
assert(x == 10 and y == nil)
x,y,z = unpack(a, 10, 11)
assert(x == 10 and y == 11 and z == nil)
a,x = unpack{1}
assert(a==1 and x==nil)
a,x = unpack({1,2}, 1, 1)
assert(a==1 and x==nil)

if not _no32 then
  assert(not pcall(unpack, {}, 0, 2^31-1))
  assert(not pcall(unpack, {}, 1, 2^31-1))
  assert(not pcall(unpack, {}, -(2^31), 2^31-1))
  assert(not pcall(unpack, {}, -(2^31 - 1), 2^31-1))
  assert(pcall(unpack, {}, 2^31-1, 0))
  assert(pcall(unpack, {}, 2^31-1, 1))
  pcall(unpack, {}, 1, 2^31)
  a, b = unpack({[2^31-1] = 20}, 2^31-1, 2^31-1)
  assert(a == 20 and b == nil)
  a, b = unpack({[2^31-1] = 20}, 2^31-2, 2^31-1)
  assert(a == nil and b == 20)
end

-- testing pack

a = table.pack()
assert(a[1] == nil and a.n == 0)

a = table.pack(table)
assert(a[1] == table and a.n == 1)

a = table.pack(nil, nil, nil, nil)
assert(a[1] == nil and a.n == 4)


-- testing sort


-- test checks for invalid order functions
local function check (t)
  local function f(a, b) assert(a and b); return true end
  local s, e = pcall(table.sort, t, f)
  assert(not s and e:find("invalid order function"))
end

check{1,2,3,4}
check{1,2,3,4,5}
check{1,2,3,4,5,6}


function check (a, f)
  f = f or function (x,y) return x<y end;
  for n = #a, 2, -1 do
    assert(not f(a[n], a[n-1]))
  end
end

a = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
     "Oct", "Nov", "Dec"}

table.sort(a)
check(a)

function perm (s, n)
  n = n or #s
  if n == 1 then
    local t = {unpack(s)}
    table.sort(t)
    check(t)
  else
    for i = 1, n do
      s[i], s[n] = s[n], s[i]
      perm(s, n - 1)
      s[i], s[n] = s[n], s[i]
    end
  end
end

perm{}
perm{1}
perm{1,2}
perm{1,2,3}
perm{1,2,3,4}
perm{2,2,3,4}
perm{1,2,3,4,5}
perm{1,2,3,3,5}
perm{1,2,3,4,5,6}
perm{2,2,3,3,5,6}

limit = 5000

a = {}
for i=1,limit do
  a[i] = rand()
end

table.sort(a)
check(a)

table.sort(a)
check(a)

a = {}
for i=1,limit do
  a[i] = rand()
end

i=0
table.sort(a, function(x,y) i=i+1; return y<x end)
check(a, function(x,y) return y<x end)


table.sort{}  -- empty array

for i=1,limit do a[i] = false end
table.sort(a, function(x,y) return nil end)
check(a, function(x,y) return nil end)
for i,v in pairs(a) do assert(not v or i=='n' and v==limit) end

A = {"álo", "\0first :-)", "alo", "then this one", "45", "and a new"}
table.sort(A)
check(A)

tt = {__lt = function (a,b) return a.val < b.val end}
a = {}
for i=1,10 do  a[i] = {val=rand(100)}; setmetatable(a[i], tt); end
table.sort(a)
check(a, tt.__lt)
check(a)


-- test remove
local function test (a)
  table.insert(a, 10); table.insert(a, 2, 20);
  table.insert(a, 1, -1); table.insert(a, 40);
  table.insert(a, #a+1, 50)
  table.insert(a, 2, -2)
  assert(table.remove(a,1) == -1)
  assert(table.remove(a,1) == -2)
  assert(table.remove(a,1) == 10)
  assert(table.remove(a,1) == 20)
  assert(table.remove(a,1) == 40)
  assert(table.remove(a,1) == 50)
  assert(table.remove(a,1) == nil)
end

a = {n=0, [-7] = "ban"}
test(a)
assert(a.n == 0 and a[-7] == "ban")

a = {[-7] = "ban"};
test(a)
assert(a.n == nil and #a == 0 and a[-7] == "ban")


table.insert(a, 1, 10); table.insert(a, 1, 20); table.insert(a, 1, -1)
assert(table.remove(a) == 10)
assert(table.remove(a) == 20)
assert(table.remove(a) == -1)

a = {'c', 'd'}
table.insert(a, 3, 'a')
table.insert(a, 'b')
assert(table.remove(a, 1) == 'c')
assert(table.remove(a, 1) == 'd')
assert(table.remove(a, 1) == 'a')
assert(table.remove(a, 1) == 'b')
assert(#a == 0 and a.n == nil)

a = {10,20,30,40}
assert(a[#a] == 40)
assert(table.remove(a, #a) == 40)
assert(a[#a] == 30)
assert(table.remove(a, 2) == 20)
assert(a[#a] == 30 and #a == 2)


return "OK"
