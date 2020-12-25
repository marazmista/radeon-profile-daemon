System daemon for reading info about Radeon GPU clocks and volts as well as control card power profiles so the GUI [radeon-profile](https://github.com/marazmista/radeon-profile) application can be run as normal user.

Supprts opensource xf86-video-ati and  xf86-video-amdgpu drivers.

# Dependencies

1. `qt5-base`

# Build

Type:

```
git clone https://github.com/marazmista/radeon-profile-daemon.git &&
cd radeon-profile-daemon/radeon-profile-daemon
qmake &&
make
``` 

# systemd service

There is a service file for systemd in radeon-profile-daemon/extra. If installed manually, copy service file to `/etc/systemd/system/`. After that, execute `systemctl enable radeon-profile-daemon.service` and `systemctl start radeon-profile-daemon.service` to make the daemon running.

# tmpfiles

There is a tmpfiles file that can be used by opentmpfiles or systemd-tmpfiles in radeon-profile-daemon/extra. If installed, it will make sure the /run/radeon-profile-daemon directory is created with the correct ownership and permissions.

# Links

* AUR package: https://aur.archlinux.org/packages/radeon-profile-daemon-git
* radeon-profile: https://github.com/marazmista/radeon-profile
* radeon-profile AUR package: https://aur.archlinux.org/packages/radeon-profile-git
* radeon-profile thread: http://phoronix.com/forums/showthread.php?83602-radeon-profile-tool-for-changing-profiles-and-monitoring-some-GPU-parameters
