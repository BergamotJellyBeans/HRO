# HRO File Format Specification — 第3版

仕様書改訂：第3版（2026-10-04）  
ファイル形式：HRO File Version 2

第1版の501点／秒（中心780 Hz ±250 Hz）を、現在の601点／秒
（中心周波数 ±300 Hz）に更新した。780 Hzは設定例であり固定値ではない。
中心周波数は観測設定で変更できる。Header、Validity Map、Event Mapの
構造および開始位置は変更しない。


## 1. 概要

HRO File Format は、流星電波観測（HRO: Ham-band Radio
Observation）の観測データを保存するための共通ファイルフォーマットである。

本フォーマットは、Raspberry Piを使用した観測装置、および将来開発する
Viewer / 解析ソフトウェアで共通して使用することを目的とする。

現在の運用ではPi5でHROデータを保存する。Tab5はPNG画像のみを保存し、
HROデータの保存・読み出しは行わない。フォーマット自体は装置に依存しない。

Version 2 では、1ファイルに1時間分の観測データを格納する。

ファイルは以下の領域から構成される。

1.  JSON Header
2.  Validity Map
3.  Event Map
4.  FFT Data

------------------------------------------------------------------------

## 2. 基本仕様

| 項目 | 値 |
| --- | --- |
| HRO File Version | 2 |
| 1ファイルの観測時間 | 3600秒（1時間） |
| Header Size | 8192 bytes |
| Header Encoding | UTF-8 |
| Header Padding | 0x00 |
| JSON Maximum Size | 8191 bytes |
| Validity Map Size | 512 bytes |
| Event Map Size | 3600 bytes |
| FFT Data Type | IEEE-754 FLOAT32 |
| Byte Order | LITTLE_ENDIAN |
| Record Interval | 1秒 |
| Record Slots | 3600 |

------------------------------------------------------------------------

## 3. ファイル構造

``` text
Offset 0
┌────────────────────────────────────┐
│ JSON Header                        │
│ 8192 bytes                         │
│                                    │
│ UTF-8 JSON（最大8191 bytes）       │
│ 残りを0x00で埋める                │
├────────────────────────────────────┤
Offset 8192
│ Validity Map                       │
│ 512 bytes                          │
├────────────────────────────────────┤
Offset 8704
│ Event Map                          │
│ 3600 bytes                         │
├────────────────────────────────────┤
Offset 12304
│ FFT Data                           │
│ 2404 bytes × 3600 records          │
└────────────────────────────────────┘
```

第3版の標準パラメータでは、中心周波数にかかわらず601点／秒となる。

``` text
FFT_BIN_COUNT = 601
sizeof(FLOAT32) = 4 bytes

FFT_RECORD_SIZE
    = 601 × 4
    = 2404 bytes

FFT_DATA_SIZE
    = 2404 × 3600
    = 8,654,400 bytes

FILE_SIZE
    = 8192 + 512 + 3600 + 8,654,400
    = 8,666,704 bytes
```

となる。

各領域の開始位置は、Header に記録された各領域のサイズから算出する。

``` text
VALIDITY_MAP_OFFSET = HEADER_SIZE

EVENT_MAP_OFFSET =
    HEADER_SIZE
    + VALIDITY_MAP_SIZE

FFT_DATA_OFFSET =
    HEADER_SIZE
    + VALIDITY_MAP_SIZE
    + EVENT_MAP_SIZE
```

Version 2 ではそれぞれ、

``` text
VALIDITY_MAP_OFFSET = 8192
EVENT_MAP_OFFSET    = 8704
FFT_DATA_OFFSET     = 12304
```

となる。

------------------------------------------------------------------------

## 4. JSON Header

### 4.1 ヘッダ領域と終端

ヘッダ領域はファイル先頭から8192 bytes固定とする。

- JSON本文はoffset 0からUTF-8で記録する。BOMは付けない。
- JSONの最上位はobjectとする。
- JSON本文の長さはUTF-8エンコード後のバイト数で最大8191 bytes。
- JSON本文の直後からoffset 8191まで、残りの全バイトを `0x00` で埋める。
- 最初の `0x00` をJSON本文の終端とする。最低1バイトの終端を必ず確保する。
- 整形用の空白や改行はJSON本文に含めてよい。
- 80バイトCARD、ASCII SPACEのpadding、`END_HEADER` は使用しない。
- JSON文字列内のNULは `\u0000` と表記し、実バイトの `0x00` は本文中に含めない。
- サイズ超過の場合はエラーとし、JSONを途中で切り詰めて保存しない。

```text
Offset 0                 : UTF-8 JSON本文の開始
Offset JSON_BYTE_LENGTH  : 最初の0x00（本文終端）
以降～Offset 8191        : すべて0x00
Offset 8192              : Validity Mapの開始
```

### 4.2 JSON項目

従来のHeader Key名を維持し、数値・真偽値はJSONの型で記録する。
`NTP_SYNCED` はJSON boolean、それ以外の整数項目はJSON numberとする。
緯度・経度・周波数分解能などはJSON number、名称・時刻はstringとする。
以下の項目を必須とする。時刻同期をしていない場合の
`NTP_SYNC_TIME_UTC` は空文字列とする。

以下は中心780 Hzで観測した場合の例である。

```json
{
  "HRO_FILE_VERSION": 2,
  "HEADER_SIZE": 8192,
  "VALIDITY_MAP_SIZE": 512,
  "EVENT_MAP_SIZE": 3600,
  "EVENT_TYPE": "UINT8_BIT_FLAGS",
  "DATA_TYPE": "FLOAT32",
  "BYTE_ORDER": "LITTLE_ENDIAN",
  "FILE_START_UTC": "2026-10-04T00:00:00Z",
  "FILE_HOUR_LOCAL": "2026-10-04T09:00:00",
  "TIME_ZONE": "Asia/Tokyo",
  "START_TIME_SOURCE": "NTP",
  "NTP_SYNCED": true,
  "NTP_SYNC_TIME_UTC": "2026-10-03T23:55:11Z",
  "TIME_CONFLICTS": 0,
  "OBSERVER": "Bergamot JellyBeans[Matsue Astronomy Club]",
  "LOCATION": "Yonago, Tottori, JAPAN",
  "LONGITUDE": 133.345833,
  "LATITUDE": 35.443611,
  "RECEIVER": "RTL-SDR Blog V4",
  "ANTENNA": "Small Loop MK-3A",
  "RF_FREQUENCY_HZ": 53372000,
  "SDR_LO_FREQUENCY_HZ": 53612000,
  "SDR_SAMPLE_RATE_HZ": 960000,
  "FFT_SIZE": 8192,
  "FFT_OUTPUT_RATE_HZ": 8192,
  "FFT_RESOLUTION_HZ": 1.0,
  "FFT_CENTER_HZ": 780,
  "FFT_RANGE_HZ": 300,
  "FFT_MIN_HZ": 480,
  "FFT_MAX_HZ": 1080,
  "FFT_BIN_COUNT": 601,
  "RECORD_INTERVAL_SEC": 1,
  "RECORD_SLOTS": 3600,
  "DEVICE": "Raspberry Pi 5",
  "SOFTWARE": "Pi5-HRO",
  "SOFTWARE_VERSION": "0.1"
}
```

装置・観測条件・時刻に依存する値は、例をそのまま使わず実際の値を記録する。
中心周波数が900 Hzなら、`FFT_MIN_HZ` は600、`FFT_MAX_HZ` は1200となる。
文字列には日本語などのUTF-8文字を使用できる。
追加された未知のキーはReaderで無視してよい。重複キーは認めない。

### 4.3 読み込み時の検証

1. ファイル先頭から8192 bytesを読み取る。不足する場合はエラー。
2. この領域内で最初の `0x00` を探す。見つからない場合は終端なしのエラー。
3. 終端以降のヘッダ領域がすべて `0x00` であることを確認する。
4. 終端より前をUTF-8として読み、JSONパーサーで本文全体を解析する。
5. UTF-8不正、JSON構文不正、空本文、複数のJSON値、重複キーはエラー。
6. 最上位object、必須キー、各型、形式Version、領域サイズ、FFT条件を検証する。
7. ファイルサイズがHeaderから計算した期待サイズと一致することを確認する。

閉じ括弧 `}` を文字検索して終端を判断しない。文字列中にも括弧が現れるため、
最初の `0x00` で範囲を区切ってからJSONとして検証する。
Version 2以外の形式は対応する別のReaderで扱い、Version 2として解釈しない。

------------------------------------------------------------------------

## 5. Validity Map

Validity Mapは、各1秒スロットに対応するFFTデータが有効かどうかを示す。

1秒につき1 bitを使用する。

``` text
RECORD_SLOTS = 3600

必要bit数 = 3600 bits
必要byte数 = 450 bytes
```

Version 2ではValidity Map領域として512 bytesを確保する。

最初の450 bytesを3600秒分のValidity情報として使用し、残り62
bytesはReservedとする。

### 5.1 Bit Order

Validity Mapのbit順は **LSB-first** とする。

``` text
byte[0]
    bit 0 → second 0
    bit 1 → second 1
    bit 2 → second 2
    ...
    bit 7 → second 7

byte[1]
    bit 0 → second 8
    ...
    bit 7 → second 15
```

一般式：

``` text
byte_index = second / 8
bit_index  = second % 8
```

該当bitが、

``` text
0 = FFTデータ無効
1 = FFTデータ有効
```

を表す。

C/C++での判定例：

``` cpp
bool valid =
    (validity_map[second / 8] &
     (1u << (second % 8))) != 0;
```

Reserved領域はVersion 2では `0x00` とする。

------------------------------------------------------------------------

## 6. Event Map

Event Mapは、各1秒スロットに対応するイベント情報を格納する。

``` text
EVENT_MAP_SIZE = 3600 bytes
RECORD_SLOTS   = 3600
```

したがって、

``` text
1 second = 1 byte (uint8_t)
```

とする。

Event値は **bit flags** として扱う。

### 6.1 Event Flags

Version 2では以下を定義する。

| Bit | Value | Event | 内容 |
| --- | --- | --- | --- |
| bit 0 | `0x01` | VISUAL | 人間が眼視で流星を確認 |
| bit 1 | `0x02` | Reserved | 将来拡張用 |
| bit 2 | `0x04` | Reserved | 将来拡張用 |
| bit 3 | `0x08` | Reserved | 将来拡張用 |
| bit 4 | `0x10` | Reserved | 将来拡張用 |
| bit 5 | `0x20` | Reserved | 将来拡張用 |
| bit 6 | `0x40` | Reserved | 将来拡張用 |
| bit 7 | `0x80` | Reserved | 将来拡張用 |

イベントなしは、

``` text
0x00 = NONE
```

とする。

例えば、

``` text
EVENT_MAP[927] = 0x01
```

の場合、ファイル開始から927秒目に眼視流星が記録されたことを示す。

Event Mapはbit
flagsであるため、将来複数種類のイベントを定義した場合、同一秒に複数イベントを同時記録できる。

Version 2で未定義のbitは0として書き込む。

------------------------------------------------------------------------

## 7. FFT Data

FFT Data領域には1秒ごとのFFTスペクトルを格納する。

Version 2の基本条件は以下とする。中心周波数 `FFT_CENTER_HZ` は
観測設定で変更でき、780 Hzに固定しない。保存する周波数範囲は、
設定された中心周波数を基準に次のように決める。

```text
FFT_RANGE_HZ = 300
FFT_MIN_HZ = FFT_CENTER_HZ - FFT_RANGE_HZ
FFT_MAX_HZ = FFT_CENTER_HZ + FFT_RANGE_HZ
FFT_BIN_COUNT = (FFT_MAX_HZ - FFT_MIN_HZ) / FFT_RESOLUTION_HZ + 1
              = 601  （FFT_RESOLUTION_HZ = 1.0）
```

以下は中心周波数を780 Hzに設定した場合の記録例であり、
480～1080 Hzは固定の保存範囲ではない。

``` text
FFT_SIZE=8192
FFT_CENTER_HZ=780
FFT_RANGE_HZ=300
FFT_MIN_HZ=480
FFT_MAX_HZ=1080
FFT_BIN_COUNT=601

DATA_TYPE=FLOAT32
BYTE_ORDER=LITTLE_ENDIAN

RECORD_INTERVAL_SEC=1
RECORD_SLOTS=3600
```

`FFT_OUTPUT_RATE_HZ` はFFT入力へ供給する信号のサンプルレートを表す。
ファイルのレコード出力周期は `RECORD_INTERVAL_SEC=1` であり、
8192レコード／秒を意味しない。

保存するFFT値には、画面のDisplay Levelによる表示用補正を加えない。
FLOAT32という型だけでは値の尺度（線形パワー、振幅、dBなど）は決まらない。
値の尺度・正規化・基準についてはPi5の保存処理を実装する際に確認し、
仕様へ追記する。

1レコードは、

``` text
601 × FLOAT32
```

で構成される。

FLOAT32は4 bytesであるため、

``` text
RECORD_SIZE = 601 × 4
            = 2404 bytes
```

となる。

FFT Data領域は、

``` text
record[0]       second 0
record[1]       second 1
...
record[3599]    second 3599
```

の順に格納する。

各record内のFFT値は、

``` text
FFT_MIN_HZ → FFT_MAX_HZ
```

の順に格納する。

------------------------------------------------------------------------

## 8. 時刻とスロット

1ファイルは1時間単位とする。

``` text
RECORD_INTERVAL_SEC = 1
RECORD_SLOTS = 3600
```

slot番号はファイル開始時刻からの経過秒と一致する。

例：

``` text
09:00:00 → slot 0
09:00:01 → slot 1
...
09:15:27 → slot 927
...
09:59:59 → slot 3599
```

Validity Map、Event Map、FFT Dataはすべて同一のslot番号に対応する。

例えばslot 927について、

``` text
Validity Map → slot 927のFFTデータが有効か
Event Map    → slot 927にイベントが存在するか
FFT Data     → slot 927のスペクトル
```

を表す。

------------------------------------------------------------------------

## 9. ファイル書き込み

観測中のファイルは、未完了ファイルであることを識別できるよう `.tmp`
拡張子を使用することを推奨する。

例：

``` text
20260920_0900.hro.tmp
```

1時間分の記録が完了し、必要なデータが正常に書き込まれた後、

``` text
20260920_0900.hro
```

へrenameする。

これによりViewerや解析ソフトウェアは、原則として `.hro`
ファイルのみを処理対象とすることができる。

------------------------------------------------------------------------

## 10. 設計方針

HRO File Formatは以下を基本方針とする。

-   観測データと観測条件を同一ファイルに保存する
-   Headerのみからデータ構造と観測条件を把握できるようにする
-   Tab5、Raspberry Pi、Windows Viewer等で共通利用できる形式とする
-   観測データの欠損と、正当なゼロ値を区別できるようにする
-   眼視観測などのイベント情報をFFTデータと同じ時間軸で保存する
-   将来のViewer、解析処理、AI解析で利用できる構造とする
-   Headerは人間が直接確認しやすいテキスト形式とする
-   バイナリデータ領域は単純な構造とし、高速かつ容易にアクセスできるようにする

------------------------------------------------------------------------

## 11. Version

``` text
HRO File Format Version : 2
Specification Language  : Japanese
Document Edition        : 3
Revision Date           : 2026-10-04
Status                  : Draft
```

本仕様はHROシステム開発中のVersion 2
Draftであり、実装検証に伴って改訂する可能性がある。


## 12. 第2版の変更点と互換性

| 項目 | 第1版 | 第2版 |
| --- | --- | --- |
| FFT範囲（中心780 Hz） | ±250 Hz | ±300 Hz |
| FFT_MIN_HZ | 530 | 480 |
| FFT_MAX_HZ | 1030 | 1080 |
| FFT_BIN_COUNT | 501 | 601 |
| 1レコード | 2,004 bytes | 2,404 bytes |
| FFT Data全体 | 7,214,400 bytes | 8,654,400 bytes |
| ファイル全体 | 7,226,704 bytes | 8,666,704 bytes |

第3版ではヘッダの80バイト固定長CARD方式を廃止し、UTF-8のJSONに変更する。
JSON本文の直後からヘッダ領域末尾までを0x00で埋める。
中心周波数は設定可能、保存範囲は中心±300 Hz、601点／秒とする。
Validity Map、Event Map、FFT Dataの構造と開始位置は変更しない。

## 13. 第3版の変更点と互換性

| 項目 | 第2版（形式Version 1） | 第3版（形式Version 2） |
| --- | --- | --- |
| ヘッダ本文 | 80 bytes固定長CARD | UTF-8 JSON object |
| ヘッダ終端 | END_HEADER CARD | 最初の0x00 |
| 未使用領域 | ASCII SPACE | 0x00 |
| 数値・真偽値 | テキスト | JSONのnumber・boolean |
| ヘッダ領域 | 8192 bytes | 8192 bytes（変更なし） |
| FFT点数 | 601点／秒 | 601点／秒（変更なし） |
| ファイル全体 | 8,666,704 bytes | 8,666,704 bytes（変更なし） |

本改版は仕様書の変更であり、既存の `HroWriter` のJSON対応は別途実装する。
形式Version 1のCARDヘッダと、形式Version 2のJSONヘッダは互換ではない。
Readerは先頭の内容を確認して形式を判定し、JSONの場合は
`HRO_FILE_VERSION` を検証する。未対応形式を推測で読み込まない。
