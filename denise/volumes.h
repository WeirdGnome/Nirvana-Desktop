// Nirvana Desktop - denise: dyski, płyty i zaszyfrowane wolumeny (LUKS)
//
// Wykrywanie: lsblk (dane z udev). Akcje: UDisks2 przez D-Bus (libdbus) - ta sama
// usługa, której używają GNOME i KDE; działa na Devuanie (z elogind) i na Rocky.
// Hasło LUKS idzie prosto w wywołaniu D-Bus, nie trafia do argumentów procesów.
//
// Co pokazujemy na biurku:
//   * nośniki wymienne i podłączane na gorąco (USB, karty), napędy optyczne z płytą,
//   * partycje wewnętrzne z systemem plików lub LUKS, o ile nie są częścią systemu
//     (ani one, ani nic "pod nimi" nie jest zamontowane poza /media, /run/media, /mnt).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platinum.h"

namespace vol {

struct Volume {
  std::string dev;          // urządzenie, np. /dev/sdb1 (dla LUKS: kontener)
  std::string kname;        // nazwa jądra (sdb1, sr0, dm-0) - do ścieżki obiektu UDisks2
  std::string disk_kname;   // cały dysk (sdb) - do wysuwania / wyłączania
  std::string name;         // nazwa wyświetlana
  std::string fstype;       // system plików (po odszyfrowaniu - wewnętrzny)
  std::string mountpoint;   // "" gdy niezamontowany
  std::string uuid;         // stabilny identyfikator (do porównań między skanami)
  uint64_t size = 0;
  bool removable = false;   // USB, karta, płyta - można bezpiecznie wysunąć
  bool optical = false;
  bool encrypted = false;   // kontener LUKS
  bool unlocked = false;    // ... odblokowany (istnieje urządzenie odszyfrowane)
  bool unformatted = false; // nośnik wymienny bez systemu plików (do sformatowania)
  std::string clear_kname;  // urządzenie odszyfrowane (dm-0)
  plat::Icon icon = plat::Icon::HardDisk;
};

std::vector<Volume> scan();

// Akcje zwracają "" przy powodzeniu albo komunikat błędu po polsku.
std::string mount(const Volume &v, std::string &mountpoint);
std::string unmount(const Volume &v);
std::string unlock(const Volume &v, const std::string &passphrase);
std::string lock(const Volume &v);
// Bezpieczne odłączenie: odmontuj, zamknij LUKS, potem wysuń płytę albo wyłącz dysk USB.
std::string eject(const Volume &v);
// Formatowanie (UDisks2 Block.Format): type = vfat, exfat, ext4, ntfs.
std::string format(const Volume &v, const std::string &type, const std::string &label);

bool udisks_available();
std::string human_size(uint64_t bytes);   // "16 GB"

} // namespace vol
