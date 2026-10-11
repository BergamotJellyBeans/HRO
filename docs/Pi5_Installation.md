# Pi5-HRO セットアップ手順書

Raspberry Pi 5で受信・表示・PNG保存までを構築する手順です。コマンドは特記したものを除きPi5の端末、またはSSH接続した端末で実行します。

**この手順は公開ソースの確認に基づく構築用の下書きです。新規OSからの実機通し検証は未実施です。** 筆者の実機で使ったNTPサービス名、RTL-SDRライブラリの版、OSの版は未確認です。一般的な構築例と、実際に確認した開発履歴を区別しています。

## 1. 準備するものとネットワーク

- Raspberry Pi 5、電源、アクティブクーラー
- Raspberry Pi OS Desktop 64-bit
- NVMe SSDと対応する接続機器
- RTL-SDR Blog V4、53.372 MHz受信用アンテナ
- 同じLANからアクセスできるMacまたはWindows PC

ブラウザからの操作、SSHによる開発、SMB共有にLAN接続が必要です。NTPによる時刻同期には、インターネット上またはLAN内のNTPサーバに接続できることが必要です。OS・パッケージ・ソースの取得にはインターネット接続を使用します。

OS初期設定でユーザーとSSHを設定します。以下の`LOGIN_USER`と`PI5_IP_ADDRESS`を自分の環境に置き換えます。

```bash
# MacまたはWindows側で実行
ssh LOGIN_USER@PI5_IP_ADDRESS
```

VS CodeのRemote - SSHを使えば、Pi5上のソースを編集し、Pi5でコンパイル・デバッグできます。接続先のIPアドレスはルータのDHCP予約などで固定すると扱いやすくなります。

構築時の環境を記録します。

```bash
cat /etc/os-release
uname -m
hostname -I
```

`uname -m`は64-bit環境で`aarch64`になります。

## 2. 必要なパッケージ

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build git gdb pkg-config \
  libusb-1.0-0-dev libcairo2-dev libcpp-httplib-dev \
  libwebsocketpp-dev libboost-system-dev nlohmann-json3-dev \
  samba usbutils fonts-dejavu-core
```

| パッケージ | 用途 |
| --- | --- |
| build-essential / cmake / ninja-build | C++のコンパイルとビルド手順の生成・実行 |
| git / gdb / pkg-config | ソース管理、デバッグ、ライブラリ設定の取得 |
| libusb-1.0-0-dev | RTL-SDRライブラリをビルドするためのUSBライブラリ |
| libcairo2-dev | hro-pngによる画像生成 |
| libcpp-httplib-dev | HTTPサーバ |
| libwebsocketpp-dev / libboost-system-dev | LIVEデータと音声を送るWebSocket |
| nlohmann-json3-dev | 設定・状態などのJSON処理 |
| samba | Mac・Windowsからのファイル共有 |
| usbutils | USB機器の確認 |
| fonts-dejavu-core | PNGの文字描画用フォント |

依存関係は公開ソースのCMake定義とincludeから整理しました。OSの版によるパッケージ名・ビルド差異は実機検証で確認してください。

## 3. NVMeのマウントと実行ユーザー

NVMeを`/mnt/hro`にマウントします。既存データを消すフォーマット操作はこの手順には含みません。

```bash
lsblk -f
```

出力の`NAME`でNVMeのext4パーティション（例：`nvme0n1p1`）を探し、その行の`UUID`列を確認します。SSD全体の`nvme0n1`ではなく、`FSTYPE`が`ext4`のパーティションのUUIDを使います。SDカード側の`mmcblk0p2`（`rootfs`）や`mmcblk0p1`（`bootfs`）のUUIDとは区別してください。

出力例は次のとおりです（関係する列のみ抜粋。UUIDは説明用の架空の値です）。

```text
NAME        FSTYPE LABEL    UUID
nvme0n1p1   ext4   HRO_DATA 12345678-1234-1234-1234-123456789abc
```

UUIDはファイルシステムごとに異なり、再フォーマットすると変わります。この例の値をそのまま使わず、自分の`lsblk -f`出力からコピーしてください。確認できたら、マウント先を作成し、設定を編集します。

```bash
sudo mkdir -p /mnt/hro
sudo nano /etc/fstab
```

[設定例](../examples/pi5/fstab.example)を参考に、実際のext4パーティションのUUIDでマウント行を追加します。既存のOSの行は残します。

設定例の`YOUR_NVME_FILESYSTEM_UUID`は置き換え用の文字列です。必ず、確認したUUIDに置き換えてください。上記の架空のUUIDを使った記載例は次のとおりです。実際には自分のUUIDに置き換えてください。

```text
UUID=12345678-1234-1234-1234-123456789abc /mnt/hro ext4 defaults,nofail 0 2
```

保存してnanoを終了したら、次を実行します。

```bash
sudo systemctl daemon-reload
sudo mount /mnt/hro
findmnt /mnt/hro
```

`findmnt /mnt/hro`で`TARGET`が`/mnt/hro`、`SOURCE`が対象のNVMeパーティション（例：`/dev/nvme0n1p1`）、`FSTYPE`が`ext4`であることを確認します。`mount`でエラーが出た場合や、`findmnt`で何も表示されない場合は、ここで止めてUUIDと設定を確認してください。マウント前に`/mnt/hro`配下へファイルを作ると、SSDではなくOS側の領域へ保存されるため、以降はNVMeが正しくマウントされたことを確認してから進めます。

サービス定義の実行ユーザーは`hro`です。存在しない場合に作成します。

```bash
id hro || sudo useradd --system --user-group --create-home \
  --home-dir /var/lib/hro --shell /usr/sbin/nologin hro
sudo install -d -o hro -g hro -m 0775 /mnt/hro/observations
sudo install -d -o "$USER" -g "$(id -gn)" -m 0755 /mnt/hro/development
```

構成は次のとおりです。

```text
/mnt/hro/
├── development/HRO/     ソースとビルド結果
└── observations/        観測記録、SMB共有先
/etc/hro/config.ini      観測設定
```

## 4. 正確な時刻を得るためのNTP

Raspberry Pi OSでは標準の`systemd-timesyncd`による時刻同期を利用する構成です。まず既存の同期状態を確認します。既に同期していれば、追加インストールや同期先の変更は不要です。

流星エコーを他の観測地点と比較するため、観測開始前に時計の同期を確認します。「サービスが動いている」ことと「時計が同期済み」であることは分けて確認します。

### 標準の時刻同期を確認する

```bash
timedatectl status
systemctl status systemd-timesyncd.service --no-pager
timedatectl timesync-status
```

`timedatectl status`で次の状態を確認します。

```text
System clock synchronized: yes
              NTP service: active
```

`timesync-status`では同期先サーバと時刻差などを確認できます。ネットワーク接続直後は、同期するまで少し待って再確認します。Pi5-HROではOSが同期したシステム時計を利用します。

### 同期が無効になっている場合

別の同期サービスを使用していないことを確認してから有効にします。

```bash
sudo timedatectl set-ntp true
sudo systemctl enable --now systemd-timesyncd.service
```

タイムゾーンが日本以外になっている場合は、表示時刻の設定を変更します。タイムゾーン設定とNTP同期は別の設定です。

```bash
sudo timedatectl set-timezone Asia/Tokyo
```

標準サービスが削除された環境などで、`systemd-timesyncd.service`が存在せず、別の時刻同期サービスも使っていない場合に限り、次を実行してから有効化します。

```bash
sudo apt install -y systemd-timesyncd
```

固定の同期先が必要な場合は`/etc/systemd/timesyncd.conf`またはそのdrop-inで設定し、サービスを再起動します。通常はOS側の設定をそのまま使用します。

### chronyを既に使っている場合

```bash
chronyc tracking
chronyc sources -v
```

選択された同期元、時刻差、同期状態を確認します。timesyncd用の`timedatectl timesync-status`はchronyの確認には使いません。

未同期なら、ネットワーク、DNS、NTPサーバへのUDP 123番の通信、サービスのログを確認します。NTPは時刻を補正しますが、特定の精度を保証するものではありません。現行のHROサービス定義は時刻同期完了を待つ設定ではないため、初回起動前や再起動後に同期状態を確認してください。

TODO（実機記録）：使用サービス、同期先、OSの版、同期確認の出力を追記する。

参考：[Debian timedatectlマニュアル](https://manpages.debian.org/bookworm/systemd/timedatectl.1.en.html)。

## 5. RTL-SDR Blog V4のドライバとUSBアクセス権

V4に対応したライブラリを使用します。以下は、新規環境でメーカーのLinux手順に沿ってOsmocom版をビルドする例です。筆者の実機で導入した版は未確認です。既存のRTL-SDR環境では、パッケージ版と手動導入版を重ねる前に現在の導入方法を確認してください。

```bash
mkdir -p ~/src
cd ~/src
git clone https://github.com/osmocom/rtl-sdr.git
cd rtl-sdr
git rev-parse HEAD
cmake -S . -B build -G Ninja -DINSTALL_UDEV_RULES=ON
cmake --build build -j2
sudo cmake --install build
sudo install -m 0644 rtl-sdr.rules /etc/udev/rules.d/rtl-sdr.rules
sudo ldconfig
```

`git rev-parse HEAD`の出力を導入版の記録として残します。

公式udevルールではUSBアクセスに`plugdev`グループを使っています。端末のユーザーとサービスユーザーを追加します。

```bash
getent group plugdev || sudo groupadd --system plugdev
sudo usermod -aG plugdev "$USER"
sudo usermod -aG plugdev hro
sudo udevadm control --reload-rules
```

USBを挿し直し、SSHを再接続してグループ変更を反映します。サービス起動後にグループを追加した場合はサービスの再起動も必要です。

```bash
lsusb
id
id hro
sudo -u hro /usr/local/bin/rtl_test -s 960000
```

`rtl_test`はCtrl+Cで終了します。HROを起動する前に行い、同じSDRを二つのプログラムから同時に開かないようにします。サービス起動後の診断では先に`hro-engine`を停止します。

### DVB-Tドライバ競合への対処

`rtl_test`で次のエラーが出た場合、`-6`はUSBインターフェースが使用中であることを示します。DVB-T用のカーネルドライバ、または別のSDRプログラム（`hro-engine`など）が機器を占有している可能性があります。このエラーだけでは、どちらが原因かは確定できません。

```text
usb_claim_interface error -6
Failed to open rtlsdr device #0.
```

まず、次のコマンドでDVB-Tドライバがロードされているか確認します。

```bash
lsmod | grep -E 'dvb_usb_rtl28xxu|rtl2832'
```

出力では、先頭が`dvb_usb_rtl28xxu`の行を確認します。例えば、次の行はテレビ受信用のカーネルドライバがロードされていることを示します。

```text
dvb_usb_rtl28xxu       49152  1
```

右端の`1`は使用参照数で、接続機器の台数ではありません。この出力だけではRTL-SDRの占有までは断定できませんが、`rtl_test`の`Kernel driver is active, or device is claimed by second instance of librtlsdr.`という表示および`usb_claim_interface error -6`と合わせて、DVB-Tドライバとの競合が強く疑われます。以下の設定と再起動後に、他の条件を変えずに`rtl_test`が成功するかを確認します。

テレビ受信用ドライバが機器を占有する場合は、観測専用機で次の設定を作ります。これはメーカーのLinux手順にも記載されています。

```bash
printf '%s\n' 'blacklist dvb_usb_rtl28xxu' | \
  sudo tee /etc/modprobe.d/hro-rtl-sdr.conf
```

`blacklist dvb_usb_rtl28xxu`が表示され、エラーがないことを確認します。ブラックリストは既にロードされたドライバを直ちに解除するものではありません。設定後、次のコマンドで再起動します。SSH接続は切断されます。

```bash
sudo reboot
```

Pi5の起動後、SSHで再接続して、`rtl_test`を実行します。

```bash
sudo -u hro /usr/local/bin/rtl_test -s 960000
```

テストが動き始めたら、Ctrl+Cで終了します。

| 症状 | 確認する点 |
| --- | --- |
| USB機器が見つからない | ケーブル、電源、USB接続、`lsusb` |
| Permission denied | udevルール、plugdev所属、再接続、hroユーザーでのテスト |
| Device busy / claim interface失敗 | DVB-Tドライバ、別のSDRプログラム、hro-engineの稼働 |
| 開けるが受信がおかしい | V4対応ライブラリ、リンク先、アンテナ、周波数・Gain |

TODO（実機記録）：実際に導入したライブラリのコミットと、競合対処が必要だったかを追記する。

参考：[RTL-SDR Blog V4公式ガイド](https://www.rtl-sdr.com/V4/)、[Osmocomのudevルール](https://github.com/osmocom/rtl-sdr/blob/master/rtl-sdr.rules)。

## 6. HROの取得とビルド

```bash
cd /mnt/hro/development
git clone https://github.com/BergamotJellyBeans/HRO.git
cd HRO
git rev-parse HEAD
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
ldd build/app/pi/hro-engine/hro-engine
```

`ldd`で`not found`がないこと、`librtlsdr`のリンク先が意図した導入先であることを確認します。メモリ不足でコンパイラが終了した場合は`-j1`で再実行します。SSH先でビルドするため、Mac・Windows側にC++環境を作る必要はありません。

### 正常な出力例（2号機での確認）

2026年10月11日、`pi5-hro-02`では次の10件のテストがすべて`Passed`となりました。以下は実機出力から実行時間を省略したものです。

```text
Test project /mnt/hro/development/HRO/build
 1/10 Test  #1: observation_file_manager ......... Passed
 2/10 Test  #2: fs4_rotator ...................... Passed
 3/10 Test  #3: decimator_15 ..................... Passed
 4/10 Test  #4: resampler_16_125 ................. Passed
 5/10 Test  #5: nco_shifter ...................... Passed
 6/10 Test  #6: dsp_chain ........................ Passed
 7/10 Test  #7: hann_window ...................... Passed
 8/10 Test  #8: complex_fft ...................... Passed
 9/10 Test  #9: hro_display_config ............... Passed
10/10 Test #10: hro_config ....................... Passed

100% tests passed, 0 tests failed out of 10
```

正常の目安は、実行したテストがすべて成功し、失敗が0件であることです。テスト名・件数・実行時間はHROの版や環境によって変わる場合があります。

同じ実機の`ldd`出力では、`not found`はなく、RTL-SDRライブラリは今回ソースからインストールした`/usr/local/lib/librtlsdr.so.0`を参照していました。以下はメモリアドレスを省略した出力例です。

```text
linux-vdso.so.1
librtlsdr.so.0 => /usr/local/lib/librtlsdr.so.0
libstdc++.so.6 => /lib/aarch64-linux-gnu/libstdc++.so.6
libm.so.6 => /lib/aarch64-linux-gnu/libm.so.6
libgcc_s.so.1 => /lib/aarch64-linux-gnu/libgcc_s.so.1
libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6
libusb-1.0.so.0 => /lib/aarch64-linux-gnu/libusb-1.0.so.0
/lib/ld-linux-aarch64.so.1
libudev.so.1 => /lib/aarch64-linux-gnu/libudev.so.1
libcap.so.2 => /lib/aarch64-linux-gnu/libcap.so.2
```

この結果は、テストの成功と`hro-engine`の共有ライブラリ解決が正常であることを示します。実際のSDR受信やPNG保存の動作確認は、以降の手順で行います。

## 7. 観測設定と保存先

```bash
sudo install -d -o hro -g hro -m 0755 /etc/hro
sudo install -o hro -g hro -m 0644 \
  examples/pi5/config.ini.example /etc/hro/config.ini
sudo nano /etc/hro/config.ini
```

観測者、場所、座標、アンテナ、ファイル接頭辞を変更します。`frequency_hz=53372000`は53.372 MHz、`sdr_gain=402`は40.2 dBです。設定の詳細は[設定・共有の手順](Pi5_Setup.md)を参照してください。

### 保存先の確認

hro-pngのPNG・JSON保存先とArchive Viewerの参照先は、`/mnt/hro/observations`です。第3節で作成した保存先に、実行ユーザーが書き込めることを確認します。

```bash
sudo -u hro test -w /mnt/hro/observations && echo '保存先への書き込み権限あり'
```

この手順の動作確認対象はPNG・JSON保存です。仕様書にある`.hro`保存については、対応実装の公開状態と使用する版を確認してから追加します。仕様書があることだけでは、その機能が公開ソースで動くとは判断できません。

### 公開ソースの旧保存先との互換設定

> **旧版向けの参考情報：** 2026年10月11日の保存先統一修正（[コミット `d46808a`](https://github.com/BergamotJellyBeans/HRO/commit/d46808a8fad6ab20f7875677d5bba23c7ec37117)）を含むソースからビルドする場合、以下の互換リンク設定は適用不要です。保存・参照先は`/mnt/hro/observations`に統一されています。ビルド日ではなく、修正コミットを含む版かどうかで判断してください。

この手順を作成した時点の公開ソースでは、hro-pngとArchive Viewerに`/mnt/hro/png`が残っています。実際の保存領域を`/mnt/hro/observations`へ統一するには、新規構築で`png`が存在しない場合、互換リンクを作ります。

```bash
# /mnt/hro/png が存在しない新規環境だけで実行
sudo ln -s /mnt/hro/observations /mnt/hro/png
readlink -f /mnt/hro/png
sudo -u hro test -w /mnt/hro/observations
```

既存の`png`ディレクトリがある場合は、上のリンク作成を実行せず、停止中にデータ移行方法を確認してください。既存のファイルを上書き・削除する手順にはしていません。

## 8. ブラウザ操作用の制御スクリプト

```bash
sudo install -o root -g root -m 0755 \
  scripts/hro-control.sh /usr/local/sbin/hro-control
sudo visudo -f /etc/sudoers.d/hro-control
```

次を記述します。hroユーザーには指定の制御コマンドだけを許可します。

```sudoers
hro ALL=(root) NOPASSWD: /usr/local/sbin/hro-control start, /usr/local/sbin/hro-control stop, /usr/local/sbin/hro-control restart, /usr/local/sbin/hro-control shutdown
```

```bash
sudo chmod 0440 /etc/sudoers.d/hro-control
sudo visudo -cf /etc/sudoers.d/hro-control
sudo -u hro sudo -n -l
```

Web画面の観測開始・停止・設定反映・Pi5終了に使います。現在のWeb画面は同じLANで使う前提です。終了操作もできるため、ルータのポート転送でインターネットへ公開する構成にはしません。

## 9. サービス登録と起動

以下の`systemd/...`はHROのソースフォルダからの相対パスです。ホームディレクトリ（`~`）で実行すると、`install: cannot stat ...: No such file or directory`となり、サービス定義をコピーできません。そのまま後続のコマンドを実行すると、`Unit ... does not exist`や`Unit ... not found`となります。

まずHROのソースフォルダへ移動し、3ファイルが存在することを確認します。ファイルが見つからない場合は、ここで止めて取得先を確認してください。

```bash
cd /mnt/hro/development/HRO
ls systemd/hro-engine.service systemd/hro-web.service systemd/hro-png.service
```

3ファイルが表示されたら、サービス定義をコピーします。

```bash
sudo install -o root -g root -m 0644 \
  systemd/hro-engine.service systemd/hro-web.service \
  systemd/hro-png.service /etc/systemd/system/
```

コピー時にエラーがないことを確認してから、次の登録・起動コマンドを実行します。

```bash
sudo systemctl daemon-reload
sudo systemctl enable hro-png.service hro-web.service hro-engine.service
sudo systemctl start hro-png.service hro-web.service
sudo systemctl start hro-engine.service
systemctl status hro-engine hro-web hro-png --no-pager
```

サービスは`hro`ユーザーで実行し、作業フォルダは`/mnt/hro/development/HRO`です。画像テンプレートなども使うため、ビルド済み実行ファイルだけでなく、リポジトリをこの場所に置きます。

```bash
journalctl -u hro-engine -u hro-png -u hro-web -b -n 100 --no-pager
```

設定ファイルの読み取り・保存、USB接続、画像保存についてエラーがないことを確認します。

## 10. ブラウザの接続先

Mac・Windowsのブラウザで開きます。`PI5_IP_ADDRESS`は実際のIPアドレスに置き換えます。

| 画面 | URL |
| --- | --- |
| Station Settings | `http://PI5_IP_ADDRESS:8080/settings` |
| Live Monitor | `http://PI5_IP_ADDRESS:8080/monitor` |
| Archive Viewer | `http://PI5_IP_ADDRESS:8080/archive` |

HTTPはTCP 8080、LIVEのWebSocketはTCP 8081、音声はTCP 8082を使います。ファイアウォールがある場合は、観測PCからこれらのポートへ接続できるように設定します。プロセス間ではUDP 50000（LIVE FFT）、50001（PNG）、50002（音声）を使用します。

設定保存と設定反映・再起動は別操作です。Station Settingsで保存後、必要に応じて「Apply Settings & Restart」を実行します。音声はブラウザ側でユーザー操作が必要になる場合があります。

## 11. SMBによる観測記録の共有

[SMB設定例](../examples/pi5/smb.conf.example)を`/etc/samba/smb.conf`へ追加します。全体を置き換えず、既存の設定を残します。

```bash
sudo smbpasswd -a hro
sudo testparm -s
sudo systemctl enable --now smbd
sudo systemctl restart smbd
```

MacはFinderの「サーバへ接続」で`smb://PI5_IP_ADDRESS/HRO`、Windowsはエクスプローラで`\\PI5_IP_ADDRESS\HRO`を指定します。Sambaユーザー`hro`と、上で設定したパスワードで接続します。

`HRO`は`/mnt/hro/observations`を共有します。設定例の`HRO-NVMe`はNVMe全体を共有する追加項目です。開発ファイルも共有したい場合に使用します。

## 12. 初回の観測確認

1. `timedatectl`などで時計の同期を確認する。
2. 三つのサービスが起動していることを確認する。
3. Live MonitorでWaterfallと受信状態を確認する。
4. 起動直後の端数ブロックに続き、最初から最後まで観測した20分ブロックを待つ。
5. `/mnt/hro/observations`のPNG・JSONを確認し、Archive Viewerでも表示する。
6. 完全な20分ブロックでReceived 1200/1200・Missing 0を確認する。起動・停止を含むブロックとは区別する。
7. SMB経由で同じファイルを開けることを確認する。
8. 再起動後もマウント、同期、サービス起動を確認する。日付境界をまたぐ連続観測も確認する。

## 13. 再現確認の記録欄

| 項目 | 記録 |
| --- | --- |
| OSの版・64-bit | TODO |
| HROのコミット | TODO：`git rev-parse HEAD` |
| RTL-SDRライブラリのコミット | TODO |
| NTPサービス・同期先・確認出力 | TODO |
| DVB-T競合対処の要否 | TODO |
| NVMeのマウント確認 | TODO |
| ビルド・ctest結果 | TODO |
| ブラウザ・音声・SMB確認 | TODO |
| 完全な20分ブロックのReceived/Missing | TODO |
| 日付境界・再起動後の確認 | TODO |

この欄を埋めた検証環境の版を明示すると、同じ環境を構築する方が比較しやすくなります。
