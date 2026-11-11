// Nirvana Desktop - denise: disks, discs and encrypted volumes (LUKS)
//
// Detection: lsblk (data from udev). Actions: UDisks2 through D-Bus (libdbus) - the same
// service GNOME and KDE use; works on Devuan (with elogind) and on Rocky.
// The LUKS password goes straight into the D-Bus call, never into process arguments.
//
// What is shown on the desktop:
// * removable and hot-plugged media (USB, cards), optical drives with a disc,
// * internal partitions with a file system or LUKS, unless they are part of the system
// (neither they nor anything "below" them is mounted outside /media, /run/media, /mnt).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platinum.h"

namespace vol {

struct Volume {
  std::string dev;          // the device, e.g. /dev/sdb1 (for LUKS: the container)
  std::string kname;        // kernel name (sdb1, sr0, dm-0) - for the UDisks2 object path
  std::string disk_kname;   // the whole disk (sdb) - for ejecting / powering off
  std::string name;         // display name
  std::string fstype;       // file system (after decryption - the inner one)
  std::string mountpoint;   // "" when not mounted
  std::string uuid;         // a stable identifier (for comparisons between scans)
  uint64_t size = 0;
  bool removable = false;   // USB, card, disc - can be safely ejected
  bool optical = false;
  bool encrypted = false;   // LUKS container
  bool unlocked = false;    // ... unlocked (a decrypted device exists)
  bool unformatted = false; // a removable medium without a file system (to be formatted)
  std::string clear_kname;  // the decrypted device (dm-0)
  plat::Icon icon = plat::Icon::HardDisk;
};

std::vector<Volume> scan();

// Actions return "" on success or an error message in the interface language.
std::string mount(const Volume &v, std::string &mountpoint);
std::string unmount(const Volume &v);
std::string unlock(const Volume &v, const std::string &passphrase);
std::string lock(const Volume &v);
// Safe removal: unmount, close LUKS, then eject the disc or power off the USB disk.
std::string eject(const Volume &v);
// Formatting (UDisks2 Block.Format): type = vfat, exfat, ext4, ntfs.
std::string format(const Volume &v, const std::string &type, const std::string &label);

bool udisks_available();
std::string human_size(uint64_t bytes);   // "16 GB"

} // namespace vol
