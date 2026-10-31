# Tab5-HRO 導入手順（Windows）

WindowsでESP-IDFを導入し、M5Stack Tab5向けのHROをビルド・書き込みする手順です。

**状態：下書き。Windowsでの通し検証は未実施です。** Mac側の保存済みTab5設定はESP-IDF 5.5.4、対象はESP32-P4です。依存関係はESP-IDF 5.5系（5.5.0以上、5.6.0未満）を要求しています。

取得先の方針はTab5専用リポジトリ`BergamotJellyBeans/Tab5-HRO`です。2026年10月12日にHTTPSで取得できることを確認しました。ただし専用リポジトリのmainは初期コミット`88ebd23`で、継続開発版はHROの`tab5-development`（`6759742`）にあります。現在の実機と同じ機能を再現するには、専用リポジトリの別ブランチへ開発版と必要な共有コードを整理し、Tab5で検証してから取得先を確定します。Pi5用HRO mainには変更を加えません。

## ESP-IDFを使う理由

Tab5-HROは、ESP32-P4のUSBホストでRTL-SDRのIQデータを受信し、DSP処理、画面表示、音声出力、SD保存、ネットワーク通信を組み合わせるアプリケーションです。現在の実装では、これらを構成する基盤としてESP-IDFを使用しています。

開発にあたっては、M5Stack Tab5とRTL-SDRで単体動作するSDRを実現した[OrcSDR](https://github.com/hardcoreerik/OrcSDR)を参考にしました。また、Tab5-HROのUSB受信には、同じ作者の[esp-rtl-sdr](https://github.com/hardcoreerik/esp-rtl-sdr)を使用しています。このドライバはESP-IDF向けのUSBホストコンポーネントであり、その受信基盤を利用することも、ESP-IDFを使う理由の一つです。OrcSDRを参考にしたことと、実際に依存するesp-rtl-sdrコンポーネントを区別して記載しています。

- **RTL-SDRのUSB受信**：使用する`esp_rtl_sdr`は、ESP32-P4の高速USBホストを対象とするESP-IDFコンポーネントです。既存の受信実装を、その前提となる環境でビルドします。
- **並行処理の管理**：受信、DSP、表示、音声などの処理をFreeRTOSのタスクやキューで構成し、優先度とデータの受け渡しを管理します。
- **Tab5固有のハードウェア設定**：LCD、PSRAM、SDカード、ESP32-C6とのSDIO接続などを、ESP-IDFのドライバと`sdkconfig`で設定します。
- **ビルド環境の再現**：ESP-IDFの版、依存コンポーネント、機器設定をソースとともに管理し、MacとWindowsで同じ構成を再現します。本手順の基準はESP-IDF 5.5.4です。

ArduinoでもFreeRTOSやESP-IDFの機能を利用できるため、Arduinoでは実現できないという意味ではありません。このプロジェクトでは、USB受信コンポーネントと機器設定を直接扱う現在の構成を再現するため、Arduino IDEではなくESP-IDFの開発環境で導入します。

OrcSDRを参考にした経緯は開発者の確認に基づき、コンポーネントと機器設定の説明は現在のソース構成に基づいています。OrcSDRから個別のDSP処理やタスク構成を移植したかどうかは、ここでは断定していません。

## 1. 準備するもの

- Windows PC（検証対象はWindows 11を想定）とインターネット接続
- M5Stack Tab5と電源
- データ通信に対応したUSBケーブル
- Tab5用microSDカード
- RTL-SDR Blog V4とアンテナ（単体観測を確認する場合）

コマンドは特記したものを除き、Windowsの「ESP-IDF Command Prompt」で実行します。PowerShellとcmdでは文法が異なるため、この手順ではcmdに統一します。

作業フォルダ例は`C:\HRO-work`です。ESP-IDF、Python、プロジェクトのパスには空白・日本語・括弧を含めず、短いパスを使います。

## 2. ESP-IDF 5.5.4の導入

[Espressif公式Windows導入手順](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/get-started/windows-setup.html)を開き、Windows Installer Downloadから公式インストーラーを取得します。

1. インストーラーでESP-IDF **5.5.4**を選びます。最新版を自動選択せず、使用する版を確認します。
2. インストール先は例として`C:\Espressif`配下を使います。
3. 対象チップの選択がある場合はESP32-P4を含めます。
4. Python、Git、コンパイラー、CMake、Ninjaなど必要なツールを導入します。
5. 導入後、5.5.4に対応する「ESP-IDF Command Prompt」を開きます。

画面の項目名はインストーラーによって異なる場合があります。5.5.4が選択できない場合は、その時点で版の選択方法を確認します。6系への変更は本手順の対象外です。

## 3. 開発環境の確認

```bat
idf.py --version
git --version
python --version
```

`idf.py --version`で5.5.4であることを確認します。コマンドが認識されない場合は、通常の端末ではなくESP-IDF専用の端末を開いているか確認してください。

新しい端末を開いたときも、ESP-IDF環境を有効にする必要があります。導入時に作成された専用ショートカットを使います。

## 4. HROソースの取得

**この節は専用リポジトリの内容を整備後に確定します。現時点では最新機能の導入用として実行しません。**

専用リポジトリの存在・取得を確認するコマンドは次のとおりです。現在は初期版を取得します。

```bat
mkdir C:\HRO-work
cd /d C:\HRO-work
git clone https://github.com/BergamotJellyBeans/Tab5-HRO.git
cd Tab5-HRO
git rev-parse HEAD
```

現在の専用版はプロジェクトルートに`CMakeLists.txt`、`sdkconfig`、`main`があります。継続開発版の共有コードを含む配布構成を確定した後、この節と以降の作業フォルダを合わせます。Macの`build`フォルダや開発環境キャッシュはコピーしません。

## 5. Tab5設定の確認とビルド

```bat
cd /d C:\HRO-work\Tab5-HRO
findstr /C:"CONFIG_IDF_TARGET=" sdkconfig
```

`CONFIG_IDF_TARGET="esp32p4"`を確認します。配布済みの`sdkconfig`にはTab5固有のLCD・PSRAM・ESP32-C6接続などの設定があります。初回導入で無条件に`idf.py set-target`を実行して設定を初期化しません。設定が存在しない場合や対象が違う場合は先へ進まず確認します。

```bat
idf.py build
```

初回は依存コンポーネントのダウンロードが必要です。ビルド成功を示す`Project build complete`と書き込みコマンドの案内を確認します。途中にエラーがあれば、その最初のエラーから確認し、書き込みへ進みません。

## 6. microSDカードの準備

Tab5で読み込めるファイルシステム形式・容量は実機検証で確定します。既存データがある場合は先に退避してください。

次の画像をmicroSDカードのルート（最上位）へコピーします。

```text
C:\HRO-work\HRO\ui\assets\radio_meteor_observation_base_1280x720.png
```

Mac側の統合版では設定ファイルはSD上の`/tab5-hro/config.ini`、観測PNGは`/tab5-hro/`配下です。カードの準備後、安全に取り外してTab5へ挿入します。

## 7. USB接続とCOMポートの確認

Tab5のPC接続用USBポートへ、データ通信対応ケーブルで接続します。PC用ポートとRTL-SDR用ホストポートの位置、必要なダウンロードモード操作は、使用機の公式資料とWindows実機で確認して追記します。

Windowsのデバイスマネージャーで接続前後の変化を確認し、Tab5に対応する`COM番号`を記録します。以下の`COM5`は例です。自分のポート番号へ置き換えてください。

ポートが表示されなければ、ケーブル・接続ポート・認識状態を確認します。他のターミナルソフトがそのCOMポートを開いている場合は終了します。

## 8. 書き込み

ESP-IDF Command Promptで、プロジェクトフォルダにいることを確認して実行します。

```bat
cd /d C:\HRO-work\Tab5-HRO
idf.py -p COM5 flash
```

書き込みは現在のTab5ファームウェアを更新します。書き込みエラーがないことを確認してからログ表示へ進みます。

```bat
idf.py -p COM5 monitor
```

monitorはログを表示し続けるため、プロンプトが戻らないのが正常です。終了は`Ctrl+]`です。コンパイル・書き込み・ログ確認を分け、どの段階で失敗したか分かるようにします。

## 9. 起動・受信・保存の確認

以下はローカル統合版の確認項目です。整備後の専用版とWindows実機で、実際の画面・ログを照合して追記します。

- LCDが表示され、ベース画像と個体識別名`Tab5-HRO-XXXXXX`が確認できる。
- SDカードの画像と設定を読める。
- 単体観測では、RTL-SDRを起動前に接続し、認識とウォーターフォールの更新を確認する。
- SDRを起動時に認識しない場合のPi5ターミナルモードと単体観測を区別する。
- Wi-Fi、時刻同期、観測者・場所・受信条件を設定し、保存・再起動後も保持される。
- 単体観測でPNG保存と再読込を確認する。統合版READMEでは毎時00・20・40分に区切り、起動直後の部分ブロックを保存しないため、最初のPNGまで約40分かかる場合がある。
- Pi5ターミナルモードではTab5自身の観測PNGを保存しない。

Windowsでの設定画面への接続方法、SSID、URL、正常ログは対象ソースと実機出力を確認して確定します。

## 10. ソース更新

配布ブランチとビルド手順の確定後に追記します。更新前には`git status --short`で手元の変更を確認し、未コミットの変更を保持します。正常なビルドを確認してから書き込みへ進みます。

## 11. 検証記録

| 項目 | 記録 |
| --- | --- |
| Windowsの版 | 未確認 |
| ESP-IDF | 5.5.4を基準、Windows導入未確認 |
| Gitコミット | 初期確認：6759742、実際の取得時にも記録 |
| Tab5の版・識別番号 | 未確認 |
| USB接続・COM番号 | 未確認 |
| SD形式・容量 | 未確認 |
| ビルド・書き込み | 未確認 |
| 単体受信・PNG保存 | 未確認 |
| Pi5ターミナル動作 | 未確認 |

## 参考

- [ESP-IDF 5.5 Windows導入・ビルド・書き込みの公式手順](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/get-started/windows-setup.html)
- Gitの`tab5-development`ブランチ：`app/tab5/README.md`、`main/idf_component.yml`、`sdkconfig`、`sdkconfig.defaults`
