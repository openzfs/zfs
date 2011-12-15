This is basic information about the 'sharesmb' option in ZFS
On Linux by Turbo Fredriksson <turbo@bayour.com>.

REQUIRENMENTS
============================================================
1. Samba (doh! :)
   * I'm using version 3.3.3, but any 3.x should
     probably work. Please update the tracker with
     info..

2. Samba will need to listen to 'localhost' (127.0.0.1),
   because that is hardcoded into the zfs module/libraries.

3. Some configuration settings in samba:

	usershare max shares = 100

4. A ZFS filesystem or more to export.


TESTING
============================================================
Once configuration in samba have been done, test that this
works with the following commands (in this case, my ZFS
filesystem is called 'share/Test1'):

	net -U root -S 127.0.0.1 usershare add Test1 /share/Test1 "Comment: /share/Test1" "Everyone:F"
	net usershare list | grep -i test
	net -U root -S 127.0.0.1 usershare delete Test1

The first command will create a user share that gives
everyone full access. To limit the access below that,
use normal UNIX commands (chmod, chown etc).
