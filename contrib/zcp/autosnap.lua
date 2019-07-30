-- Recursively snapshot every dataset with a given property
--
-- Usage: zfs program <pool> autosnap.lua -- [-n] [-p <property>] <snapshot>

results = {}

args = ...
argv = args["argv"]
usage = [[


usage: zfs program <pool> autosnap.lua -- [-n] [-p <property>] <snapshot>

	-n: performs checks only, does not take snapshots
	-p <property>: property to check. [default: com.sun:auto-snapshot]
	<snapshot>: root snapshot to create [example: tank/data@backup]
]]

property = "com.sun:auto-snapshot"
noop = false
root_snap = nil

for i, arg in ipairs(argv) do
	if arg == "-n" then
		noop = true
	elseif arg == "-p" then
	elseif argv[i-1] == "-p" then
		property = arg
	else
		root_snap = arg
	end
end

if root_snap == nil or property == nil then
	error(usage)
end

root_ds_name = ""
snap_name = ""
for i = 1, #root_snap do
	if root_snap:sub(i, i) == "@" then
		root_ds_name = root_snap:sub(1, i-1)
		snap_name = root_snap:sub(i+1, root_snap:len())
	end
end

function auto_snap(root)
	auto, source = zfs.get_prop(root, property)
	if auto == "true" then
		ds_snap_name = root .. "@" .. snap_name
		err = 0
		if noop then
			err = zfs.check.snapshot(ds_snap_name)
		else
			err = zfs.sync.snapshot(ds_snap_name)
		end
		results[ds_snap_name] = err
	end
	for child in zfs.list.children(root) do
		auto_snap(child)
	end
end

auto_snap(root_ds_name)
err_txt = ""
for ds, err in pairs(results) do
	if err ~= 0 then
		err_txt = err_txt .. "failed to create " .. ds .. ": " .. err .. "\n"
	end
end
if err_txt ~= "" then
	error(err_txt)
end

return results
