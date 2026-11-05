Положите бинарники iPXE в tftp-корень ($LXBOOT_PREFIX/tftp):

  undionly.kpxe   — BIOS PXE
  ipxe.efi        — UEFI x86_64
  boot.ipxe       — генерируется lxbootd

Сборка:

  git clone https://github.com/ipxe/ipxe.git
  cd ipxe/src
  make bin/undionly.kpxe EMBED=/usr/share/lxboot/boot.ipxe
  make bin-x86_64-efi/ipxe.efi EMBED=/usr/share/lxboot/boot.ipxe

Цепочка: PXE ROM → undionly.kpxe/ipxe.efi → boot.ipxe → iSCSI LUN клиента.
