#!/usr/bin/env bash
# Wait for sshd on the live image; expect is the fallback if offline inject failed.
set -eu

PUBKEY=$(cat ~/.ssh/id_ed25519.pub)
PUBKEY_B64=$(printf '%s\n' "$PUBKEY" | base64 | tr -d '\n')
TIMEOUT=300

wait_for_ssh() {
  local tries="$1"
  local i
  for i in $(seq 1 "$tries"); do
    if ssh -o ConnectTimeout=2 -o BatchMode=yes root@vm0 true 2>/dev/null; then
      echo "NetBSD vm0 SSH is up"
      return 0
    fi
    sleep 2
  done
  return 1
}

# Offline injection is the default.  First boot can take a few minutes.
if wait_for_ssh 90; then
  exit 0
fi

echo "SSH not up after offline-key wait; trying expect fallback" >&2
if ! command -v expect >/dev/null 2>&1; then
  sudo apt-get install -y expect >/dev/null || true
fi
if ! command -v expect >/dev/null 2>&1; then
  echo "NetBSD firstboot: SSH still down and expect is not installed" >&2
  exit 1
fi

# virsh console attaches to the guest serial.  Live image login is
# root with an empty password.
expect <<EOF
set timeout $TIMEOUT
spawn sudo virsh console openzfs --force
expect {
  -re {login:} {}
  timeout { puts "timeout waiting for login"; exit 1 }
}
send "root\r"
expect {
  -re {Password:} { send "\r"; exp_continue }
  -re {# |\\$ } {}
  timeout { puts "timeout waiting for shell"; exit 1 }
}
send "mkdir -p /root/.ssh /etc/ssh\r"
expect -re {# |\\$ }
send "echo $PUBKEY_B64 | base64 -d >> /root/.ssh/authorized_keys\r"
expect -re {# |\\$ }
send "chmod 700 /root/.ssh; chmod 600 /root/.ssh/authorized_keys\r"
expect -re {# |\\$ }
send "grep -q '^PermitRootLogin yes' /etc/ssh/sshd_config || echo 'PermitRootLogin yes' >> /etc/ssh/sshd_config\r"
expect -re {# |\\$ }
send "grep -q '^sshd=YES' /etc/rc.conf || echo 'sshd=YES' >> /etc/rc.conf\r"
expect -re {# |\\$ }
send "grep -q '^dhcpcd=YES' /etc/rc.conf || echo 'dhcpcd=YES' >> /etc/rc.conf\r"
expect -re {# |\\$ }
send "dhcpcd || true\r"
expect -re {# |\\$ }
send "/etc/rc.d/sshd start || /etc/rc.d/sshd restart || true\r"
expect -re {# |\\$ }
send "exit\r"
expect eof
EOF

if wait_for_ssh 30; then
  exit 0
fi

echo "NetBSD firstboot: SSH still down" >&2
exit 1
