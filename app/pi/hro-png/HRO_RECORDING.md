# Pi5 HRO記録

hro-engineがFFT生成直後、UDP送信前にHROへ同期保存する。
hro-pngは従来のPNG・JSONのみを保存する。追加サービスは不要。
保存先ディレクトリは /mnt/hro/observations/YYYY/YYYYMM/YYYYMMDD/ のまま。
601点のFFTにミリ秒時刻・シーケンスを付け、同じ秒のFFTもすべて保存する。
正常停止・時間帯変更で確定し、再起動時は既存ファイルを保護して.partNを作る。
1秒に1FFTの場合は1時間約8.72 MB。

詳しくは [形式仕様](../../../docs/HRO_File_Format_Specification.md) を参照。

## 検証・反映

Mac上の編集だけではPi5は変わらない。反映は明示的な許可後に行う。
新旧engine/pngの混在は同じ保存先への書き込みを招くため、反映時は両方を
停止してから両方のバイナリを更新する。既存ファイルは移動・削除しない。

Pi5でのビルド・テスト対象はhro-engine、hro-png、test_hro_v2、
test_observation_file_manager。Pythonテストは tests/test_read_hro.py。
JSONライブラリnlohmann/json.hppは引き続き必要。

## 読み込み

```sh
python3 tools/read_hro.py FILE.hro
python3 tools/read_hro.py FILE.hro --record 0
python3 tools/read_hro.py LEGACY.hro --second 927
```

--recordは物理レコード番号で、時刻・シーケンス・FFTを表示する。
書き込み中の.tmpとの競合を避け、正常停止後または確定済みファイルを読む。
