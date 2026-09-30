# experiment-camera-pipe2

カメラドライバーを実装する際に使った実験用ディレクトリです。STM32N6570-DKのIMX335カメラをPipe1/2で取得し、LCDへ連続表示します。Pipe構成、センサーのCSI設定、撮像バッファ、クロップ方法を切り替えて比較できます。

## 表示の合格条件

IMX335のテストパターンを無効にした状態で、実景がLCDに連続表示されることを確認します。dual表示では左右の領域に同じ実景が出て、格子などの直線が走査線ごとにずれず、少なくとも60秒間フリーズや再起動なくフレームカウンターが進むことを合格条件とします。

カラーバーや一様パターンは、段差の位置を調べるための診断入力です。パターンだけが表示されている状態は実景の撮像確認にはなりません。段差を調べるときは、実景とは別に格子チャートまたはカラーバーを使います。

## ビルド設定

| CMake設定 | 既定値 | 選択肢 |
| --- | --- | --- |
| `PIPE2_PIPE_MODE` | `dual` | `dual`、`single` |
| `PIPE2_IMX335_PROFILE` | `stock` | `stock`（BT1600）、`mipi891`（BT900） |
| `PIPE2_BUFFER_MODE` | `sram` | `sram`、`psram` |
| `PIPE2_CROP_MODE` | `downsize` | `downsize`、`integer4`（dualのみ）、`native`（singleのみ） |
| `PIPE2_IMX335_TEST_PATTERN_MODE` | `-1` | `-1`で無効、`0..11`で内蔵パターン |

カメラ初期化と表示処理は専用タスクで実行します。起動設定とPipe1 VSYNC／Pipe2 frameカウンターをUARTに出力します。

```sh
make -C userspace/experiment-camera-pipe2 generate
make -C userspace/experiment-camera-pipe2 build
make -C userspace/experiment-camera-pipe2 monitor  # RAM実行より先に起動
make -C userspace/experiment-camera-pipe2 ram-run
```

## 既知の結果

- `stock`、`dual`、`sram`、`downsize`の組み合わせでプレビューが始まり、Pipe1 VSYNCとPipe2 frameの両カウンターが増えることを確認済みです。
- `mipi891`（BT900）はカウンターが進んでもLCD映像が乱れることがあります。BT900を評価するときは`stock`を基準に、他の設定をそろえてBT900だけを切り替え、LCDの実映像も確認してください。
