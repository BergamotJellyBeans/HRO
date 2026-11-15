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


## 最新30日分と過去データ追加

初期一覧は、Pi5のJST日付で今日を含む30暦日。PNG / JSONとHRO / FFTは
それぞれ Load Older Archives (+30 Days) で、直前の30暦日を追加する。
例：2026-11-08に開いた場合は2026-10-10〜2026-11-08。
次は2026-09-10〜2026-10-09を追加する。件数やデータのある日数ではない。
期間表示は取得済み範囲全体。追加時も選択中の画像／FFTページを保持する。
空の30日間を挟んでも古いファイルがあれば追加ボタンは有効。
古い対象ファイルがなければボタンを無効にする。取得失敗時はカーソルを進めない。
日付ディレクトリだけを調べ、期間外のJSON/HROヘッダは開かない。
保存ファイルの削除・移動・保存期間制限は行わない。

一覧APIは配列からオブジェクトに変更：items、before、next_before、start_date、
end_date、has_more。任意のbefore=YYYYMMDDは、その日を含まない直前30日を返す。
before未指定時のみ現在のJST日付が基準になる。無効な日付はHTTP 400。
HROのRefreshは既に読み込んだ全期間を同じ日付境界で再取得する。
ページを開いたまま日付が変わった場合は、ブラウザを再読込すると新しい今日を基準に戻る。
対象は標準のYYYY/YYYYMM/YYYYMMDD配下の観測ファイル。
API変更を含むためmain.cpp、archive_hro.cpp/.hpp、archive.html、archive_hro.jsを
必ず同時に反映する。

開発用テスト：

```sh
node tests/test_archive_paging.js
```

Nodeの追加ライブラリは不要。暦日境界・空期間・不完全ファイル・不正カーソルは
既存のtest_archive_hroへ追加してあり、Pi5では同テストを再ビルドして検証できる。


## ArchiveのDisplay Level（2026-11-14）

HRO / FFTのDisplay Level −／＋で1 dB刻み、−30〜＋30 dBを調整し、
Reset to 0 dBで補正なしに戻す。読み込み済みFFTを使い、そのページ全体の
ウォーターフォールと選択中のスペクトルを即時再描画する。
両グラフは保存値＋Display Levelを−20〜30 dBの範囲で描画し、範囲外は端にクリップする。
スペクトル上部にも適用した補正値を明記する。
設定はArchive画面内だけで保持し、ファイル・ページ移動でも維持する。
ブラウザ再読込では0 dBに戻る。LIVE設定APIや観測ファイルへの書き込みは行わない。
HRO・既存PNG・JSONは変更せず、PNG書き出し機能も追加しない。
反映対象はui/web/archive.htmlとui/web/archive_hro.jsだけ。Webのビルド・再起動不要。
検証はnode tests/test_archive_paging.jsで表示レベルと既存ページ操作を確認する。


## PNG画面のHRO再描画・表示レベル維持（2026-11-14）

PNG / JSON画面でも日時選択時にHROから自動描画する。レベルは−30〜＋30 dBで、
別の日時でも保持し、その値を画像へ適用する。読込後のレベル操作は追加通信を行わない。
画像枠・観測局情報・時刻軸は保存済みPNGを使い、ウォーターフォールとレベルグラフを
HROから再描画する。記録中の.hro.tmpや.partN.hroにも対応する。
欠落は空欄で、Available / Unavailableの秒数を表示する。
Refresh HRO dataで記録中データを再取得する。自動更新ではない。

読込中は元PNGや前の日時の画像を隠し、レベル欄を「+4 dB (loading)」などとする。
Original PNGを選んだ場合や読込失敗時はレベル欄も「Original PNG」とし、
未適用の数値補正を示さない。HRO用レベルは保持し、次の日時選択・再取得で再び適用する。
ブラウザ再読込時は0 dBへ戻る。PNG出力、LIVE設定、観測ファイルは変更しない。

専用API GET /api/archive/png/frame?file=BJ202611072200.pngは対象時間のファイルを
直接探索し、隣接時間を含め時刻とシーケンスで秒位置を決める。履歴一覧の取得は不要。
返却は16-byte header（先頭7 bytesがHROPNG1、offset 8のuint32 LEが有効秒数）、
1200-byte validity、1200×601個のfloat32 LEの順で、合計2,886,016 bytes。
完全なレコードだけを読み、末尾の書込途中レコードを除外する。元のHRO形式は変更しない。
ユーザーがPi5で読み込みの改善と、日時を切り替えた際の表示レベル適用を確認済み。

Macからの反映では接続先にhro@Pi5-HRO.localを使う。サーバー側を含む更新時は
main.cpp、archive_hro.cpp/.hpp、png_time_alignment.hpp、archive.html、archive_hro.jsと
対応テストを反映し、hro-webとtest_archive_hroをビルド・テストしてhro-webのみ再起動する。
レベル維持など画面2ファイルのみの更新は、ビルド・サービス再起動不要。

開発時の追加確認：

```sh
node tests/test_archive_paging.js
node tests/test_archive_png_level.js
ctest --test-dir build -R '^(archive_hro|png_time_alignment)$' --output-on-failure
```
