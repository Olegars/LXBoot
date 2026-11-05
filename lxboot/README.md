# LXBoot

Linux-appliance для бездисковой загрузки (аналог CCBoot). Qt-оболочка, демон `lxbootd`, CLI `lxboot`.

Пароль администратора по умолчанию: **lxboot**

## Сборка

```bash
sudo apt install -y qt6-base-dev cmake g++ libgl-dev
cd lxboot
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Запуск на рабочей машине (dry-run)

Не трогает системный DHCP, пишет конфиги в префикс:

```bash
export LXBOOT_PREFIX=/tmp/lxboot
export LXBOOT_ETC=/tmp/lxboot/etc
export LXBOOT_SOCK=/tmp/lxboot/lxbootd.sock
export LXBOOT_WINDOWED=1
mkdir -p "$LXBOOT_PREFIX" "$LXBOOT_ETC" /tmp/lxboot
./build/lxbootd &
./build/lxboot-ui
```

CLI без демона:

```bash
LXBOOT_PREFIX=/tmp/lxboot ./build/lxboot status
LXBOOT_PREFIX=/tmp/lxboot ./build/lxboot client.upsert mac=aa:bb:cc:dd:ee:ff hostname=pc01 ip=10.0.0.10
```

Боевой режим (только appliance): `LXBOOT_APPLY=1` и root — применяет ip/dnsmasq/targetcli.

## Состав

| Бинарь | Роль |
|---|---|
| `lxbootd` | демон: SQLite, lease, CoW qcow2/ZFS, генерация dnsmasq/iPXE/iSCSI |
| `lxboot-ui` | kiosk Qt (статус, сеть, DHCP, диски, образы, клиенты, журнал, службы) |
| `lxboot` | CLI к сокету демона или локальному Engine |

Write-back: qcow2 backing-файл от золотого образа (fallback без ZFS). Клиенту — iSCSI LUN через targetcli. Super Client сливает overlay в золотой образ и создаёт restore point.

ТЗ: `TZ-LXBoot.md` в корне репозитория.
