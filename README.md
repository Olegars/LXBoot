# LXBoot

Linux-appliance для бездисковой загрузки клиентских ПК (аналог CCBoot, сервер не на Windows). Qt-оболочка, демон `lxbootd`, CLI `lxboot`.

Пароль администратора по умолчанию: **lxboot**

- Код: [`lxboot/`](lxboot/)
- ТЗ: [`TZ-LXBoot.md`](TZ-LXBoot.md)

## Сборка

```bash
sudo apt install -y qt6-base-dev cmake g++ libgl-dev
cd lxboot
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Запуск (dry-run)

Не трогает системный DHCP, пишет конфиги в префикс:

```bash
export LXBOOT_PREFIX=/tmp/lxboot
export LXBOOT_ETC=/tmp/lxboot/etc
export LXBOOT_SOCK=/tmp/lxboot/lxbootd.sock
export LXBOOT_WINDOWED=1
mkdir -p "$LXBOOT_PREFIX" "$LXBOOT_ETC"
./lxboot/build/lxbootd &
./lxboot/build/lxboot-ui
```

Боевой режим (только appliance): `LXBOOT_APPLY=1` и root.
