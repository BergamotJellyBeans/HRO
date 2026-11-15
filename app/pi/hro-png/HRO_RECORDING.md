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


## PNG秒枠の時刻揺れ対策（2026-11-12）

PNGの配置だけを補正する。最初のFFTの生成完了時刻を最寄りの秒に丸め、
次からはシーケンス差に従い1FFTにつき1秒進める。欠落した番号の分は空白にする。
予測した秒との時刻差が±1500 ms以内なら、生成完了時刻の揺れとして扱う。
大きな時刻差／生成側の再起動では、後の秒へ再アンカーして警告ログを出す。
後戻りして既存PNG枠を上書きするパケットと重複パケットは拒否し、警告する。
再アンカーは大きな停止・時計変化・取得クロックの累積ずれに対する処理であり、
任意の大きな時刻変化を完全に復元できるものではない。

HROの実時刻・シーケンス・FFT、UDP形式、Display Levelは変更しない。
PNGの20分ブロック切り替えも補正後の秒で判断する。JSONの既存品質フィールドは
PNGの補正後の有効秒枠数を表す。time_placement、placement_adjusted_seconds、
alignment_resetsを追加し、補正方式と補正秒枠数・再アンカー数を記録する。
時刻補正は生成完了時刻を秒単位表示へ対応づける処理であり、
サンプル取得開始時刻を高精度に測定し直すものではない。

開始直後の20分枠や実際にパケットが欠落した枠は不完全になり得る。
既存PNG/JSONの自動再生成・上書きはしない。
テスト対象はtest_png_time_alignment、CTest名はpng_time_alignment。
