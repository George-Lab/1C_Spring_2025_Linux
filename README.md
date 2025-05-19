# 1C_Spring_2025_Linux

---

## Сборка из исходников и запуск ядра

Сначала с официального сайта [https://www.kernel.org/](https://www.kernel.org/) скачиваем
исходники ядра Linux и распаковываем их:
```shell
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.14.6.tar.xz
tar -xvf linux-6.14.6.tar.xz
```

Для сборки ядра нам потребуется конфиг `.config`. Для сборки конфига:
```shell
cd inux-6.14.6
make defconfig
```

Сборка ядра:
```shell
make -j9 all
```


Создаём директории для запуска:
```shell
cd ..
mkdir vroot
mkdir boot
```

устанавливаем результаты сборки по пути INSTALL_PATH:
```shell
cd inux-6.14.6
INSTALL_PATH=../boot make install
```

## Настройка initramfs

В директории `vroot` создадим поддиректории:
```shell
mkdir bin dev lib proc root sys tmp
```

За что будут отвечать эти поддиректории:
- `bin`: для бинарных файлов
- `lib`: для библиотек
- `dev`: в неё монтируются псевдоустройства
- `tmp`: для временной информации
- `proc`: системная виртуальная файловая система, содержит информацию о работе ядра
- `sys`: системная виртуальная файловая система, содержит информацию об оборудовании

Теперь создадим исполняемый файл `init`, который будет самым первым процессом, запускаемым
в системе.
```shell
vim init
chmod +x init
```

Содержимое файла `init`:
```shell
#!/bin/sh

mount -t devtmpfs devtmpfs /dev
mount -t tmpfs tmpfs /tmp
mount -t proc proc /proc
mount -t sysfs sysfs /sys

echo 0 > /proc/sys/kernel/printk

exec setsid sh -c 'exec sh </dev/ttyS0 >/dev/ttyS0 2>&1'
```

Возьмём утилиты из [проекта busybox](https://www.busybox.net/):
```shell
wget https://www.busybox.net/downloads/busybox-1.36.1.tar.bz2
tar -xvf busybox-1.36.1.tar.bz2
```

В директории `busybox-1.36.1` нужно сделать конфиг и собрать результаты:
```shell
make menuconfig
```

В `menuconfig` в разделе `Settings` устанавливаем опцию `Build static binary (no shared libs)`.

Теперь собираем требуемое:
```shell
make -j9 all
make install
```

Результаты сборки `busybox` появятся в директории `busybox-1.36.1/_install`.

Теперь устанавливаем утилиты в `vroot`:
```shell
cd vroot
../busybox-1.36.1/_install/bin/busybox --install ./bin
```

Теперь при помощи контейнера сложим эту структуру в файл:
```shell
find . | cpio -ov --format=newc | gzip -9 > ../initramfs
```

Запуск ядра с только что собранным `initramfs`:
```shell
qemu-system-x86_64 -kernel ./boot/vmlinuz-6.14.6 -initrd ./initramfs --enable-kvm -cpu host -nographic -append "console=ttyS0"
```

![task3](./2025-05-19%2016-47-53.gif)
![task4](./2025-05-19%2017-01-05.gif)