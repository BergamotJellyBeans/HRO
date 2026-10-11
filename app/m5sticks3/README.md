# Tab5-HRO

This ESP-IDF target lives in the same HRO repository as the Raspberry Pi apps.
It receives RTL-SDR IQ at **256 kS/s**, displays a **20-minute waterfall**, and
saves **PNG images only**. It does not link the HRO file writer or write `.hro` files.

## Build

Use the ESP-IDF 5.5 development terminal already used for Tab5:

```bash
cd app/tab5
idf.py build
```

The committed `sdkconfig` is based on the working ESP32-P4 Tab5 configuration,
including the ESP32-C6 SDIO pins. Dependencies are declared in
`main/idf_component.yml`; the first build needs access to the component registry
and the pinned esp-rtl-sdr Git repository. Do not copy a previous build cache.

After the build succeeds and the device connection has been checked:

```bash
idf.py -p PORT flash monitor
```

## SD card and settings

Copy `ui/assets/radio_meteor_observation_base_1280x720.png` from the repository
root to the SD root. Tab5 uses the 1280×720 base image and saves the upper
1280×480 observation region as PNG.
The old color bar is cleared when the plot is drawn; no edited PNG is needed.

Configuration remains in `/tab5-hro/config.ini`. Existing station and audio
settings are preserved; the display range is migrated to the fixed shared
**±300 Hz / 601 bins**. The center frequency is configurable, with 780 Hz as
the default. PNG files remain under `/tab5-hro/`, using the configured prefix.

PNG blocks end at each hour's **00, 20, and 40 minutes**. A startup partial block
is skipped, so the first PNG can take up to about 40 minutes to appear.

## ターミナルモード

起動時にUSBのSDR認識を最大8秒待ちます。認識した場合は従来の単体観測、
認識しない場合はターミナルモードになります。起動後はモードを固定します。
SDRを認識した後のストリーム開始エラーではモードを変更しません。

Tab5のWeb設定 `/station` に追加した `Pi5 IP Address` を保存して再起動します。
SDの `config.ini` には次の項目が保存されます。

```ini
[pi5]
address=192.168.0.10
```

IPアドレスは実際のPi5に合わせます。Wi-Fi接続後にUDP 50003へ登録要求を送り、
5秒ごとに更新します。登録応答の設定をLCD表示に反映し、UDP 50000のFFT・Peak・
観測時刻と50002の音声を受信します。表示時刻の出所は `[Pi5]` と表示します。
Pi5が提供する過去履歴の再送はないため、接続後のデータから20分履歴を蓄積します。
通信断の区間は空白にします。ターミナルモードではPNGの生成・SD保存を行いません。
タッチ音、音量、シャットダウン操作は継続して使えます。
SDカードはベース画像の読込とTab5自身の設定保存に使います。
スタンドアロンモードでは従来どおり20分ごとのPNG保存を行います。

Pi5の表示設定はメモリ上で使います。Tab5自身の観測設定・接続先・音声設定を
Pi5の設定で上書きしてSD保存しません。WebフォームはTab5自身の設定を編集します。

`tab5_terminal` は接続・受信と表示への受け渡し、`tab5_terminal_config` は設定応答の
解析を担当します。`core/include/hro_live_packet.h` は機種に依存しないUDPパケット解析です。

## Shared and platform-specific code

- `core/include/hro_plot.h`: 1280×480 PNG geometry, 1200-second blocks, frequency
  row mapping, relative dB palette, and level mapping; used by Pi5 and Tab5.
- `core/src/dsp/`: shared Fs/4 rotation, NCO, 64 kS/s→8192 samples/s resampler,
  and periodic Hann window. Tab5 uses the allocation-free `processOne()` API.
- `core/include/dsp/spectrum_math.h`: common CU8 normalization and power-to-dB.
- `platform/tab5/`: Tab5's 127-tap /4 frontend, 256 kS/s→64 kS/s.
- `app/tab5/`: ESP-IDF tasks, RTL-SDR/USB wiring, display, audio, Wi-Fi settings,
  RTC/NTP, LCD-to-PNG encoding, and SD operations.

Tab5 keeps ESP-DSP's optimized FFT backend; Pi5 keeps the portable complex FFT.
The 8192-point unnormalized FFT, window, IQ scale, and relative dB formula are
aligned. `Tab5Config` is distinct from the Pi5 config because it also owns audio
and local SD settings. Linux file/config code is not compiled into this target.

The shared resampler uses 154 taps per phase, compared with 64 in the previous
Tab5 implementation. NCO arithmetic also follows the common implementation.
Real-time throughput and the UI during PNG encoding must be checked on hardware.

## Verification

Portable checks on a Mac without Pi5 hardware or RTL-SDR libraries:

```bash
cmake -S . -B build -DHRO_BUILD_RASPBERRYPI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Run these commands from the repository root. The default root build still
includes the Raspberry Pi applications.

On Tab5, check stable RX/DSP sample counts, RESAMP near 8192 samples/s,
1 FFT/s, no rising drop counters, 20-minute PNG saves, axis alignment, audio,
and safe SD shutdown. Hold SHUTDOWN for two seconds; settings and PNG writes
stop before SD is unmounted. Wait for SAFE before removing the card.

Hardware throughput, clean dependency resolution, and flashing are not yet
verified for this integrated target.

## 機能別の構成

`main/main.cpp` は起動入口のみです。機能別の実装は `src/`、公開ヘッダーは `include/` に置きます。
内部のタスク共有状態は `src/internal/tab5_runtime.hpp` と `src/tab5_runtime.cpp` に置きます。`tab5_application` が初期化とタスク起動を担当します。

| ファイル | 担当 |
| --- | --- |
| tab5_radio | RTL-SDR受信制御 |
| tab5_dsp / tab5_fft | サンプル変換・FFTタスク |
| tab5_observation | 観測履歴・表示タスク・保存タイミング |
| tab5_display / tab5_controls | 画面描画・タッチ操作 |
| tab5_audio | 音声出力・音声設定保存 |
| tab5_storage / tab5_png | SDカード・PNG保存 |
| tab5_wifi / tab5_web_settings | Wi-Fi接続・Web設定画面 |
| tab5_time | RTC・NTP |
| tab5_config / tab5_config_validation | 設定の保存読込・値の検証 |
| tab5_helpers | 実機に依存しないURL解析・周波数計算・ファイル名など |
| tab5_runtime | タスク間で共有する状態と内部定数 |

各機能だけで使う状態と関数は、その実装ファイル内に置いています。
共通DSPは引き続きルートの `core`、Tab5専用受信前段は `platform/tab5` を使用します。

## Macでのテスト

リポジトリのルートから実行します。SDカード、Tab5、ESP-IDFは不要です。

```sh
cmake -S . -B build-host -DHRO_BUILD_RASPBERRYPI=OFF
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

`tab5_app_helpers` はURL解析、タッチ領域の境界、PNGファイル名のJST日付境界、
履歴リングの折り返し、受信周波数とNCO、設定範囲を確認します。
SDアクセス、描画、Wi-Fi、音声と受信の実動作は実機で確認してください。
ソース構成を変更したので、ESP-IDF側は最初に `idf.py reconfigure build` を実行します。

## コンソール表示

LCDの下部 `(10, 480)`、幅1000・高さ195にFont2で英語メッセージを9行表示します。
新しいメッセージを下に追加し、満杯になると上へスクロールします。
通常は水色、警告は黄色、エラーは赤です。同一メッセージの連続は回数をまとめます。
時刻が有効ならJSTの時分秒、それ以前は起動後の秒数を表示します。

起動・モード、Wi-Fi、SDR、Pi5登録、設定受信、観測・音声の受信開始／中断／復帰、
PNG保存、RTC/NTP、終了処理を記録します。正常な5秒ごとの登録更新やFFTごとの
処理ログはコンソールに出しません。シリアルの診断ログは従来どおり使えます。

`tab5_console` が各タスクからのメッセージを非ブロッキングのキューに集めます。
表示タスクだけがまとめてLCDへ描画し、通常の更新間隔は最短100msです。
領域はPNG保存範囲の外です。確認用デモは削除しています。

## 電源OFF

画面の `HOLD 2 SEC TO SHUTDOWN` を2秒長押しすると、PNG保存を停止し、
未保存の音声設定を保存してSDカードをアンマウントします。
成功後に `POWERING OFF...` と表示し、`M5.Power.powerOff()` で電源を切ります。
設定保存またはアンマウントに失敗した場合は電源を切らず、終了処理を再試行します。
物理電源ボタンのダブルクリックはアプリの安全終了処理を通りません。

## SDRゲイン

Web設定の `/station` にある `SDR Gain (dB)` で手動ゲインを選択します。
候補と既定値40.2dBは `core/include/hro_sdr_config.h` を参照します。
Tab5のドライバーに設定段のない48.0dBは候補から除外しています。
保存値はSD上の `tab5-hro/config.ini` の `[receiver] sdr_gain`（0.1dB単位）です。
従来のファイルにキーがない場合は40.2dBを使用します。
ゲインはconfig.iniへ保存し、次回起動時にのみ適用します。
稼働中の受信設定は変更しません。Web画面にも再起動後の反映を案内します。
FFT Display Range入力は廃止し、±300Hzを共通仕様として使用します。

## Pi5とTab5の配置ルール

- `core/include` / `core/src`: 機器に依存しない共通DSP・観測仕様
- `platform/raspberrypi`: Pi5固有のRTL-SDR・ファイル管理
- `platform/tab5`: Tab5固有の256 kS/s受信前段・SDカード・RTC
- `app/pi`: Pi5アプリの起動・機能の組み合わせ
- `app/tab5/main`: ESP-IDF起動入口・コンポーネント登録・依存関係
- `app/tab5/include`: Tab5アプリの機能別インターフェース
- `app/tab5/src`: 設定・受信タスク・表示・保存・音声・ネットワークの実装
- `app/tab5/src/internal`: アプリ内部のタスク間共有状態

ESP-IDFは `main` を標準の入口コンポーネントとして扱うため、この小さな入口は残します。
`main` の登録で `src` のファイルをビルドし、公開ヘッダーと内部ヘッダーの検索先を分離します。
表示やWi-Fiなどの機器APIを使うアプリ処理はまだ `src` にあります。
SDとRTCのアクセスは `platform/tab5` に分離しています。
今後は機器APIを専用インターフェースへ切り出して `platform/tab5` へ移す際にも、
タスク間状態を機器層から参照する依存関係を作らないようにします。

### SDカードとRTCの機器層

`platform/tab5/tab5_sdcard.cpp` が配線、電源LDO、マウント状態とPSRAMへの読み込みを扱います。
アプリの `tab5_storage.cpp` は終了前の未保存設定の保存と、終了可能状態の管理を扱います。
`platform/tab5/tab5_rtc.cpp` がM5Unifiedを使ったRTCアクセスを扱います。
アプリの `tab5_time.cpp` はNTP、UTC/JST変換、時刻検証とシステム時刻復元を扱います。
機器層はアプリの内部ヘッダーやグローバル設定を参照しません。
配置変更後は `idf.py reconfigure build` を実行し、RTC復元・NTP同期・
SD読込・PNG保存・安全終了を実機で確認してください。

### Local Display Level

The right-hand control panel provides `LEVEL-`, a signed dB value, and `LEVEL+`
above the audio controls. Each press changes waterfall brightness by 1 dB
within -30 to +30 dB; 0 dB preserves the original colour mapping. Only newly arriving waterfall columns use the adjusted level; existing columns
retain their original colours, preserving the visible history of adjustments.
The Peak graph, DSP, audio and incoming Pi5 packets remain unchanged.

Both modes save this local preference as `[display] level_db` in the Tab5
`config.ini`, together with volume/mute after two seconds without changes.
Shutdown flushes pending changes. Older configuration files default to 0 dB.
Standalone PNG files capture the adjusted waterfall colours.

## Stick VISUAL受信

SoftAP側UDP 50003にVISUAL v1受信・ACK返信を追加しました。
形式・受理範囲・重複保持・ビルド/検証手順はVISUAL_UDP.mdを参照してください。
ACKはメモリ受理を示し、SDへの観測記録保存はまだ行いません。

LCD左下はTab5-HRO-XXXXXXに加え、単体モードでは既存のv0.1とESP32-P4 + RTL-SDR、
ターミナルモードでは既存のPi5システム情報を残して表示します。
XXXXXXは既存SoftAP SSIDと同じ、Tab5のeFuseベースMAC下位3バイトの
大文字・ゼロ埋め6桁16進です。Wi-Fi名とLCD名は同じ生成関数を使います。

SoftAPへの接続/切断をLCDコンソールにAP client connected/disconnectedとして表示します。
IDは接続端末MACの下位3バイトです。Stickの場合はStick IDと一致します。
Wi-Fi接続イベントだけではStickとスマートフォンなどを区別できないため、
機種名は決めつけません。SSID検索だけでは接続通知は出ません。

StickのVISUALイベントはSDルートの `VISUAL_COUNT.csv` に受信UTC時刻・Unixミリ秒・Tab5起動ミリ秒・Stick ID・イベントIDを追記します。詳しくは `VISUAL_UDP.md` を参照してください。ACK音はメモリ受付確認のままです。

### 再起動の原因調査

起動時に `Boot reset reason=...` を記録します。30秒ごとの `Health` ログに内部ヒープ残量・最小残量・最大連続領域・main/音声タスクの最小スタック余裕（ESP-IDFではバイト）・音声キュー長を出します。通常動作の設定は変更しません。

長時間テスト時は `idf.py -B build-lcd -p <PORT> monitor --no-reset` を開いて、再起動直前のHealth・panic/backtrace・次の起動理由を保存してください。

2026-10-09の長時間テストで音声タスクの最小スタック余裕が100バイトだったため、音声タスクのスタックを3072から6144バイトに増やしました。再起動原因と確定したわけではありません。更新後のHealthログで余裕を再確認します。

### 1秒間の複数クリック試作（VISUAL version 3）

通常画面のAボタンは、最初の押下から固定1秒間の押下回数を集めて1件として送信します。押すたびに期限は延長しません。送信連番はまとまりごとに1増加し、再送では不変です。

```text
VISUAL 3 D263C4 0123456789ABCDEF 3 0000000123
```

ACK受信後、流星数と同じ回数だけ1kHz・約25msを鳴らし、音の間に約50msの無音を入れます。接続音は2kHz・各約100msを2回です。LCDは `ACK #123 x3 OK` のように表示します。Bの1秒テスト機能は削除済みです。

現段階のCSV時刻は引き続きTab5受信時刻です。最初のボタン押下時刻はまだ送信していません。

### 即時送信へ変更

Aの押下を検出するたびに流星数1の独立したイベントを送信します。1秒のクリック集計は使用しません。2回押すと2件となり、各件で送信連番が増えます。再送は同じ連番です。ACK音は1kHz・100msへ戻し、接続音は2kHz・100msを2回のままです。通信形式version 3とCSVのmeteor_count列は維持します。

即時送信版のACK確認音を1kHz・50msへ調整しました。接続音は2kHz・各100msを2回です。

通常LCDのB操作案内2行とAlive表示を削除し、起動後の通算Aボタン押下回数を大きく表示します。実際にAを押した分だけ増え、設定画面の操作は含めません。起動時0クリアです。時計・接続状態・Stick ID・ACK表示は維持します。

### 電池残量表示

LCD最下部の時計右端に電池残量（%）を表示し、5秒間隔で更新します。
M5Unified 0.2.20 の `M5PM1_Class::getBatteryVoltage` と
`Power_Class::getBatteryLevel` に合わせ、M5PM1の0x22から電圧を読み、
`(mV - 3300) × 100 / 800` を0〜100%に制限した推定値です。
充電中・負荷によって変動します。取得できない場合は `--%` を表示します。
電源・充電設定は変更しません。

電池の初回取得は起動10秒後から行い、音声初期化前・通知音の待機中／再生中は読み取りを延期します。

通知音を要求すると、内蔵LEDも接続時2回、ACK時1回（各100ms）点滅します。LEDは別タスクで動作し、音声再生の成功を保証する表示ではありません。M5PM1の公式LED_EN制御を使用します。

接続音の前に約1秒の無音PCMを流して音声回路を安定させます。接続音は2kHz・100msを2回（間隔100ms）、ACK音は1kHz・50msです。Font8の大きな押下回数表示を復元しました。

通常画面のBボタンによる1秒間隔の自動送信機能を削除しました。イベント送信はAボタン押下時のみです。Bを押しながら起動する接続先設定、および設定画面でのB保存操作は維持します。

### 待機時の省電力

LCD輝度を32/255へ下げ、押下回数は値が変わったとき（または画面を描き直したとき）だけ更新します。時計は毎秒更新します。CPU周波数はESP-IDFの電源管理で80〜160MHzに自動調整します。LGFXの通信タイミングを保つため最低80MHzとし、自動ライトスリープは無効です。ボタンの10ms監視とWi-Fi接続は維持します。電流削減量と音・通信の応答は実機で確認してください。

### 電池試験用の1分送信

通常画面でBを押すと、接続中に限り60秒間隔のVISUAL送信を開始・停止します。初回は開始60秒後、起動時は停止です。LCD上部に `TEST60s` が出ます。切断時に停止し、再接続後はBで再開します。ACK音とLEDは通常送信と同じです。試験送信は押下回数表示には加算しませんが、送信連番とTab5のCSV・マーカには記録されます。Aの手動送信と起動時Bの接続先設定は維持します。

夜間・電池試験向けに通知音のPCM振幅を8000から4000へ下げました（16bit最大振幅の約12%、振幅で従来の半分）。接続音・ACK音とも同じ振幅で、周波数と長さは維持します。
