# Zephyr RTOS for Sharp Zaurus SL-C3200

## Japanese

Sharp Zaurus SL-C3200 向けのビルド済み Zephyr RTOS イメージです。kexecboot 経由で SD カードから起動します。

**kexecboot の導入で NAND のカーネルが書き換わります。先にサービスメニュー (D + M + 電源 ON) で NAND をバックアップしてください。自己責任でお願いします。**

必要なもの: 2GB 以下の SD カード (SDHC 不可、FAT)、AC アダプタ。Microdrive は装着したままにしてください。

### 1. kexecboot の導入

1. https://github.com/greguu/linux-3.10.y-c3x00-f2fs-kexec-r0/releases/tag/r0 から
   `linux-3.10.y-c3x00-f2fs-kexec-r0.tar.gz` をダウンロード (中身は bzip2 なので `tar xjf ...` で展開)
2. 全ファイル (`updater.sh`, `zImage`, `gnu-tar`, `README`) を SD カードのルートにコピー
3. Zaurus の電源を切り、AC アダプタを接続
4. OK を押しながら電源 ON
5. 日本語メニューで 4 → 3 (SD カード) を選択
6. カーネルが書き込まれ、再起動します
7. SD カードを抜くと kexecboot のメニューが起動します

### 2. Zephyr の起動

1. [Releases](../../releases) から `zaurus-sl-c3200-zephyr-<version>.zip` をダウンロード
2. 中の `boot` ディレクトリを SD カードのルートにコピー
3. カードを挿して電源 ON。約 5 秒後に先頭のエントリが自動起動します。他を選ぶ場合は ↓ と OK で選択

| イメージ | 内容 |
|---|---|
| `lvgl_demo` | LVGL の GUI デモ (Tab / ジョグダイヤルで移動、OK で決定) |
| `lcd_console` | LCD とキーボードで使う Zephyr shell (`@` は Fn + `-`) |
| `lcd_check` | LCD のテストパターン |
| `hw_check` | 緑 / オレンジ LED の点滅 |

kexecboot のメニューに戻るには電源を入れ直してください。

### ソースからのビルド

このリポジトリは Zephyr のアプリケーションです。`main` ブランチは緑 LED を点滅させるだけの最小構成で、
サンプルは `main` から分岐した `samples/<名前>` ブランチにあります。

| ブランチ | 内容 |
|---|---|
| `main` | 緑 LED の点滅 |
| `samples/lcd_terminal` | LCD とキーボードで使う Zephyr shell。電源キー長押しで電源 OFF |
| `samples/usbh_ble_beacon` | USB の Bluetooth ドングル (CSR8510 など) で BLE の広告を受信し、RSSI から推定した距離を LCD に棒グラフで表示 |
| `samples/lvgl_clock` | LVGL のデジタル時計。AM 9:00 から始まり、ボタン (カーソルキー / ジョグと Enter / OK) で時刻を合わせる。時刻は電源を切ると消える |

Zephyr SDK と west を用意して、次のようにビルドします。zephyr は `west.yml` に書かれた
[Corgeek/zephyr-rtos](https://github.com/Corgeek/zephyr-rtos) のブランチから取得します。

```
mkdir zaurus-ws && cd zaurus-ws
git clone https://github.com/Corgeek/zaurus-reborn.git zaurus
git -C zaurus switch samples/lcd_terminal    # main のままなら LED の点滅
west init -l zaurus
west update
west build -b zaurus_sl_c3200 zaurus
```

`build/zephyr/zephyr.bin` を kexecboot から起動します。ブランチを切り替えたら `west update` と `west build -p` をやり直してください。

## English

Prebuilt Zephyr RTOS images for the Sharp Zaurus SL-C3200, booted from an SD card via kexecboot.

**Installing kexecboot overwrites the kernel in NAND. Back up the NAND first (service menu: D + M + Power On). Use at your own risk.**

Requirements: SD card of 2 GB or less (non-SDHC, FAT), AC adapter, microdrive left installed.

### 1. Install kexecboot

1. Download `linux-3.10.y-c3x00-f2fs-kexec-r0.tar.gz` from
   https://github.com/greguu/linux-3.10.y-c3x00-f2fs-kexec-r0/releases/tag/r0
   (it is actually bzip2: `tar xjf ...`).
2. Copy all files (`updater.sh`, `zImage`, `gnu-tar`, `README`) to the root of the SD card.
3. Power off the Zaurus and connect the AC adapter.
4. Hold OK and press Power On.
5. In the Japanese menu select 4, then 3 for SD card.
6. The kernel will be flashed and the Zaurus will reboot.
7. Remove the SD card. The kexecboot menu will start.

### 2. Boot Zephyr

1. Download `zaurus-sl-c3200-zephyr-<version>.zip` from [Releases](../../releases).
2. Copy the `boot` directory in it to the root of the SD card.
3. Insert the card and power on. kexecboot boots the first entry after about 5 seconds;
   use Down and OK to choose another one.

| Image | Description |
|---|---|
| `lvgl_demo` | LVGL GUI demo (Tab / jog dial to move, OK to select) |
| `lcd_console` | Zephyr shell on the LCD and keyboard (`@` is Fn + `-`) |
| `lcd_check` | LCD test pattern |
| `hw_check` | Green / orange LED blink |

Power cycle to return to the kexecboot menu.

### Build from source

This repository is a Zephyr application. The `main` branch is the smallest one, blinking the green LED;
the samples are on `samples/<name>` branches, each started from `main`.

| Branch | Description |
|---|---|
| `main` | Green LED blink |
| `samples/lcd_terminal` | Zephyr shell on the LCD and keyboard; hold the power key to power off |
| `samples/usbh_ble_beacon` | Receives BLE advertisements with a USB Bluetooth dongle (CSR8510 or similar) and shows the distance estimated from the RSSI as bars on the LCD |
| `samples/lvgl_clock` | Digital clock on LVGL; starts at 9:00 AM and is set with on-screen buttons (cursor keys or jog dial, Enter or OK); the time is lost at power off |

With the Zephyr SDK and west installed, build as below. zephyr comes from the branch of
[Corgeek/zephyr-rtos](https://github.com/Corgeek/zephyr-rtos) named in `west.yml`.

```
mkdir zaurus-ws && cd zaurus-ws
git clone https://github.com/Corgeek/zaurus-reborn.git zaurus
git -C zaurus switch samples/lcd_terminal    # stay on main for the LED blink
west init -l zaurus
west update
west build -b zaurus_sl_c3200 zaurus
```

Boot `build/zephyr/zephyr.bin` from kexecboot. After switching branches, run `west update` and `west build -p` again.

## License

Zephyr RTOS (Apache-2.0), LVGL (MIT). kexecboot is not redistributed here.
