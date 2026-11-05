Сборка appliance (этап 1 ТЗ):

1. Buildroot defconfig: x86_64, systemd, linux с TARGET_CORE, ISCSI_TARGET, bonding, vlan, NVMe, USB.
2. Пакеты: qt6base (widgets, sql, network), dnsmasq, qemu-img, targetcli, zfs (или lvm2), iproute2, lsblk.
3. Overlay:
   - /usr/bin/{lxboot,lxbootd,lxboot-ui}
   - systemd units из deploy/
   - autologin tty1 → lxboot-ui (kiosk, без bash)
4. Rootfs squashfs + overlay; данные /var/lib/lxboot на отдельном разделе.
5. Цель размера ОС 100–200 МБ без firmware NIC: резать Qt plugins, оставить qsqlite, xcb, offscreen.
