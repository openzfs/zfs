This is basic information about the 'sharesmb' option in ZFS
On Linux by Turbo Fredriksson <turbo@bayour.com>.

REQUIRENMENTS
============================================================
1. Samba (doh! :)
   * I'm using version 3.3.3, but any 3.x should
     probably work. Please update the tracker with
     info..

2. The following configuration in smb.conf

        add share command = /usr/local/sbin/modify_samba_config.pl
        delete share command = /usr/local/sbin/modify_samba_config.pl
        change share command = /usr/local/sbin/modify_samba_config.pl

	include = /etc/samba/shares.conf-dynamic

   The script (modify_samba_config.pl) comes in two
   versions, one perl and one python and can be found
   in the samba source directory:

	./examples/scripts/shares/perl/modify_samba_config.pl
	./examples/scripts/shares/python/modify_samba_config.py

   Personaly, I choosed the perl version and modified
   it slightly - to not overwrite or modify smb.conf,
   but instead the /etc/samba/shares.conf-dynamic file,
   which is completely managed by the zfs/net commands.

3. Samba will need to listen to 'localhost' (127.0.0.1),
   because that is hardcoded into the zfs module/libraries.

4. A workable root password. ZFS is using 'root' as samba
   account to add, modify and remove shares so this need
   to work.

5. A ZFS filesystem or more to export.


TESTING
============================================================
Once configuration in samba have been done, test that this
works with the following commands (in this case, my ZFS
filesystem is called 'share/Test1'):

	net -U root -S 127.0.0.1 share add Test1=/share/Test1
	net share list | grep -i test
	net -U root -S 127.0.0.1 share delete Test1

The first and second command will ask for a root password
and the second (middle) command should give at least one
line.
