# Pi5-HROの設定とファイル共有

実機の`config.ini`、`fstab`、`smb.conf`を基にした設定例です。
個人の観測情報とSSD固有のUUIDは、サンプル値へ置き換えています。

この文書は**既にOS・NVMe・HROプログラムを準備したPi5への設定方法**です。
OS導入、SSDの初期化、全依存ライブラリの導入、ソースのビルドを網羅した手順書ではありません。
以下のコマンドはPi5側で実行します。サンプルはHROリポジトリのルートから参照します。

## 1. 設定ファイルの置き場所

| 公開ファイル | Pi5での配置・使い方 |
| --- | --- |
| [config.ini.example](../examples/pi5/config.ini.example) | 編集後、`/etc/hro/config.ini`へ配置 |
| [fstab.example](../examples/pi5/fstab.example) | UUIDを変更し、NVMeの1行だけを`/etc/fstab`へ追加または既存行を修正 |
| [smb.conf.example](../examples/pi5/smb.conf.example) | HRO用共有定義を`/etc/samba/smb.conf`へ追加または既存定義を修正 |
| [systemd/](../systemd/) | 観測プログラム3本のサービス定義。配置と運用は第6節を参照 |

`fstab.example`と`smb.conf.example`は設定の一部分です。
実機の設定ファイル全体へ上書きコピーせず、既存の設定を残して必要箇所を編集します。

## 2. NVMe SSDを/mnt/hroへマウントする

実機ではext4のNVMe領域を`/mnt/hro`へマウントしています。

SSDのファイルシステムUUIDを確認します。

```bash
lsblk -f
sudo blkid
```

`fstab.example`の`YOUR_NVME_FILESYSTEM_UUID`を、対象のext4領域のUUIDへ置き換えます。
UUIDは各SSDで異なります。`/`や`/boot/firmware`のPARTUUIDを転用しません。

```text
UUID=YOUR_NVME_FILESYSTEM_UUID /mnt/hro ext4 defaults,nofail 0 2
```

編集前に既存設定を保存します。次のバックアップ名が既にある場合は、別名を使ってください。

```bash
sudo cp -p /etc/fstab /etc/fstab.before-hro
sudo mkdir -p /mnt/hro
sudo nano /etc/fstab
sudo findmnt --verify
sudo mount /mnt/hro
findmnt /mnt/hro
```

最後の出力で、対象のNVMe領域がext4としてマウントされていることを確認します。
既にマウント済みの場合、`mount`による再マウントは不要です。
実機と同じ`nofail`設定はSSDがなくてもOSの起動を妨げないためのものです。
それだけで観測プログラムの誤保存を防げるわけではないので、マウント確認後に観測を開始します。

現在の配置は次のとおりです。

```text
/mnt/hro/
├── development/
│   └── HRO/
└── observations/
```

以前の`/mnt/hro/hro-data`や`/mnt/hro/png`から、現在は`/mnt/hro/observations`へ観測記録をまとめています。
SMBの`HRO`共有もこの保存先を参照します。共有先と各プログラムの保存先をそろえてください。
`HRO-NVMe`共有では`/mnt/hro`全体を参照できます。

## 3. 観測条件のconfig.ini

サンプルは53.372 MHz受信、FFT中心780 Hz、ピーク測定範囲±50 Hzの例です。
観測者名、観測場所、緯度・経度、アンテナ、ファイル名の接頭文字列を変更します。
緯度・経度の`0.0`は仮の値です。

| 項目 | 意味・例 |
| --- | --- |
| `frequency_hz` | 観測対象のRF周波数。`53372000`は53.372 MHz |
| `sdr_gain` | 0.1 dB単位の整数。`402`は40.2 dB |
| `fft_center_hz` | DSP処理後のFFT表示中心。例：780 Hz |
| `level_peak_range_hz` | FFT中心からピークを探す範囲。`50`なら±50 Hz |
| `level_db` | 表示レベルの補正値。受信機のゲインとは別 |
| `prefix` | 保存ファイル名の接頭文字列。例：HRO |

`frequency_hz`はRTL-SDRのLO周波数そのものではありません。
受信プログラムが周波数シフトに応じてLOを設定します。

サンプルを編集用にコピーします。

```bash
cp examples/pi5/config.ini.example /tmp/hro-config.ini
nano /tmp/hro-config.ini
```

編集済みのファイルを配置します。既存の`/etc/hro/config.ini`がある場合は、先に別名で保存してください。
現在のサービス定義は`hro`ユーザーで動作するため、同ユーザー・グループが存在することを確認してから実行します。

```bash
id hro
sudo install -d -o hro -g hro /etc/hro
sudo install -o hro -g hro -m 0640 /tmp/hro-config.ini /etc/hro/config.ini
```

Web設定から保存・変更する場合にも、実行ユーザーがこのファイルへ書き込める必要があります。

## 4. Sambaでファイル共有する

実機では二つの共有を設定しています。

| 共有名 | Pi5上のフォルダ | 用途 |
| --- | --- | --- |
| `HRO` | `/mnt/hro/observations` | 観測記録用フォルダ（PNG・JSON・HROデータ） |
| `HRO-NVMe` | `/mnt/hro` | 開発用フォルダなども含むSSD領域全体 |

どちらも、認証した`hro`ユーザーによる読み書きが可能な設定です。
パスワードは設定例に含めません。

Sambaを導入し、既に存在するLinuxユーザー`hro`へSMB用パスワードを登録します。

```bash
sudo apt update
sudo apt install -y samba
id hro
sudo smbpasswd -a hro
```

`id hro`でユーザーが見つからなければ、ここでは先へ進まず、
観測プログラムの実行ユーザーと共有ユーザーの構成を先に整えます。
SMBのパスワード登録だけではLinuxユーザーは作成されません。

既存設定を保存してから、サンプルの共有定義を追加します。
既に`[HRO]`や`[HRO-NVMe]`がある場合は、その定義を編集してください。
既存の`[global]`は残します。

```bash
sudo cp -p /etc/samba/smb.conf /etc/samba/smb.conf.before-hro
sudo nano /etc/samba/smb.conf
sudo testparm -s
```

`testparm`で設定を確認した後、反映します。

```bash
sudo systemctl restart smbd
systemctl status smbd --no-pager
```

Linux側の権限も確認します。

```bash
ls -ld /mnt/hro /mnt/hro/observations
sudo -u hro test -r /mnt/hro/observations
sudo -u hro test -w /mnt/hro/observations
```

共有の`read only = no`だけでは、Linux側の権限は変わりません。
`create mask = 0664`と`directory mask = 0775`も、既存ファイルの権限を変更する設定ではありません。
SSD全体への一括権限変更は行わず、必要なフォルダごとに所有者と権限を合わせます。

### Macから接続する

Finderの「移動」→「サーバへ接続」で、次のいずれかを入力します。

```text
smb://PI5_IP_ADDRESS/HRO
smb://PI5_IP_ADDRESS/HRO-NVMe
```

`PI5_IP_ADDRESS`をPi5のIPアドレスへ置き換え、登録ユーザー`hro`とSMB用パスワードで接続します。

### Windowsから接続する

エクスプローラーのアドレス欄へ入力します。

```text
\\PI5_IP_ADDRESS\HRO
\\PI5_IP_ADDRESS\HRO-NVMe
```

同様にPi5のIPアドレスへ置き換え、`hro`で認証します。
共有フォルダから観測ファイルを参照・コピーできます。

## 5. ネットワークと時刻同期

Pi5-HROはネットワークを前提に運用しています。

- NTP：正しい観測時刻を得るため、使用するNTPサーバーへ接続できること。
- Web：操作端末からPi5へ接続できること。ブラウザ操作自体は同じLAN内で可能です。
- SMB：MacやWindowsから共有フォルダへ接続できること。
- 開発：SSH・VS Code Remote SSHでコード編集、コンパイル、デバッグを行うこと。

インターネット上のNTPサーバーを利用する場合は、インターネット接続も必要です。

```bash
timedatectl status
```

時刻、タイムゾーン、同期状態を確認します。実機で使用したNTPサービス名と
サーバー設定は今回の添付ファイルに含まれないため、この文書では確定していません。

| Web画面 | URL |
| --- | --- |
| Station Settings | `http://PI5_IP_ADDRESS:8080/settings` |
| Live Monitor | `http://PI5_IP_ADDRESS:8080/monitor` |
| Archive Viewer | `http://PI5_IP_ADDRESS:8080/archive` |

## 6. systemdサービス

リポジトリには、次のサービス定義があります。

- [hro-engine.service](../systemd/hro-engine.service)：受信・DSP・FFT
- [hro-web.service](../systemd/hro-web.service)：Web表示と操作
- [hro-png.service](../systemd/hro-png.service)：観測記録

現行定義では`User=hro`、`Group=hro`、作業フォルダは
`/mnt/hro/development/HRO`です。実行ファイルはその配下の`build/app/pi/`を参照します。
`RequiresMountsFor=/mnt/hro`により保存領域のマウントを要求します。

これらのパスにビルド済み実行ファイルがあることを確認してから、サービスを配置します。
既存のサービス定義がある場合は、内容を比較し、保存してから更新してください。

```bash
sudo install -m 0644 systemd/hro-engine.service /etc/systemd/system/
sudo install -m 0644 systemd/hro-web.service /etc/systemd/system/
sudo install -m 0644 systemd/hro-png.service /etc/systemd/system/
sudo systemctl daemon-reload
```

自動起動を設定する場合：

```bash
sudo systemctl enable hro-engine hro-web hro-png
```

観測を開始する場合は保存側とWeb側を先に起動します。

```bash
sudo systemctl start hro-png hro-web
sudo systemctl start hro-engine
systemctl status hro-engine hro-web hro-png --no-pager
```

Webのシステム操作は、サービス定義とは別に`hro-control`などの設定も必要です。
そのため、このサービス配置だけでWebの開始・停止・シャットダウンまで設定済みになるわけではありません。

## 7. 設定後の確認

1. `findmnt /mnt/hro`で保存先が対象SSDであることを確認する。
2. 観測実行ユーザーが設定を読み、保存先へ書き込めることを確認する。
3. MacまたはWindowsからSMB共有へ接続し、ファイルを参照できることを確認する。
4. NTPの同期状態を確認する。
5. ブラウザから設定画面・LIVE・Archiveを開く。
6. 完了した20分ブロックのPNG・JSONと`Received`・`Missing`を確認する。

添付された実機設定を基にしたサンプルですが、第三者の新規環境での通し検証は未実施です。
