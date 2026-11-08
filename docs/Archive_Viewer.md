# Archive Viewer（2026-11-08）

PNG / JSONとHRO / FFTを切り替えて表示する。
既存の保存処理・観測ファイル・保存先は変更しない。

## PNG / JSON

欠損のある20分枠も一覧・日時選択・前後移動に含む。
Partial received/expectedを表示し、一覧の時刻を黄色で区別する。
JSONがない／壊れていてもPNGを表示する。PNG自体のない枠は従来の.....となる。
不完全なPNGの存在する部分だけを表示し、画像やメタデータを補完・変更しない。

## HRO / FFT

ファイルを選ぶと最大1200レコードずつ生成順のウォーターフォールを表示する。
1列1レコード。横軸は時計の等間隔軸ではなく生成順である。
前後ページ、スライダー／ウォーターフォールのクリックでFFTを選択できる。
選択レコードのJSTミリ秒時刻・シーケンスと601点のスペクトルを表示する。
色／スペクトル縦軸の表示範囲は-20〜30 dBで固定し、範囲外は端にクリップする。
保存値には表示レベル補正を加えない。
シーケンスは文字列で転送し、JavaScriptの整数精度で丸めない。
シーケンス不連続数はページ内のみを数え、ファイル全体の検査結果ではない。

旧秒スロット形式も読める。無効スロットは空白とし、保存されていないシーケンスは
not storedと表示する。新形式の不正なFFT値はそのレコードを空白として警告する。
.tmpは記録中または中断ファイルとして表示する。拡張子だけで稼働状態を断定しない。
読み取り開始時のファイルサイズを上限に、完全なレコードのみ読み、途中の末尾は
除外してバイト数を警告する。Refreshで一覧・レコードを再読込する。
自動更新は行わない。時間切り替えで.tmpが改名された場合もRefreshで選び直せる。
ヘッダが不正なファイルも一覧には残し、読み取り不能の理由を表示する。

## Pi5への反映（明示的な許可後）

今回の対象ファイル：

- app/pi/hro-web/main.cpp
- app/pi/hro-web/CMakeLists.txt
- app/pi/hro-web/archive_hro.cpp（新規）
- app/pi/hro-web/archive_hro.hpp（新規）
- ui/web/archive.html
- ui/web/archive_hro.js（新規）
- tests/CMakeLists.txt
- tests/test_archive_hro.cpp（新規）
- docs/Archive_Viewer.md（新規）
- README.md

MacのHRO_pi5からPi5の/mnt/hro/development/HROへ同じ相対位置にコピーする。
.git、build、run、観測データはコピーしない。既存ソースを先にバックアップする。

Webサービスだけを停止し、コピー・ビルド・テスト後に再開する。
engine/pngは停止不要。観測記録を継続できるがWeb画面は一時的に使えない。

```sh
sudo systemctl stop hro-web
cd /mnt/hro/development/HRO
# ここで対象ソースをコピー済みであることを確認
cmake -S . -B build
cmake --build build --target hro-web test_archive_hro -j4
ctest --test-dir build -R '^archive_hro$' --output-on-failure
# 失敗した場合は再開せず、エラーを確認する
sudo systemctl start hro-web
systemctl status hro-web --no-pager
```

/archiveをブラウザで再読込し、不完全なPNG、HROページ移動、個別FFT、.tmpのRefreshを確認する。
APIは観測ファイルに読み取り専用でアクセスし、.tmpを確定・修復しない。

## Macでの検証

読み取りモジュールのC++テスト、JavaScript構文チェック、および実際のPi5共有データを
読み取り専用で使ったMac内のテスト画面で、欠損PNG・HROページ移動・FFT選択・.tmp表示を確認。
Macには既存WebサーバーのHTTP/WebSocket依存ヘッダがないため、hro-web全体のビルドと
Pi5上のHTTP統合動作は未確認。上記のPi5ビルド・テストで確認する必要がある。
