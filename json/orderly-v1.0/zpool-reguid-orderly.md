object {
  string stderr `{
  "name": "stderr",
  "description": "error output of command",
  "required": false
}`;
  array [any]* stdout `{
  "name": "stdout",
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_reguid.json",
  "name": "zpool reguid -J",
  "version": "1.0",
  "description": "Generates a new unique identifier for the pool",
  "required": true
}`;

