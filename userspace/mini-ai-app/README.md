# mini-ai-app

チュートリアル用の最小AIアプリです。カメラ（Pipe1）をLCDに表示しながら、Pipe2の画像をNeural-ART NPUで人物検出（ST YOLOX nano、person）し、検出枠を重ねて表示します。モデルは1つ、タスクは3つ（初期化、カメラ、推論）です。

作り方は[docs/tutorial](../../docs/tutorial/index.md)で章ごとに説明しています。全部入りの実装は[ai-app](../ai-app/README.md)です。

## 構成

```text
userspace/mini-ai-app/
  Makefile                       APP_TARGET、AI_MODEL_NAMES=person、ai-appのモデル生成を共有
  CMakeLists.txt                 メモリ配置の生成、モデルライブラリ、アプリ
  stm32n6570-dk-npu-ram.ld       リンカスクリプトの雛形（生成器がMEMORYと予約領域を書き換える）
  config/
    stm32n6570-dk-mini-ai-app.ioc  CubeMX設定（ai-appと同じ周辺機能）
    board_memory.json              物理メモリ領域とcommand blobの置き場所
    application_memory.json        キャプチャ/表示/推論バッファの数とサイズ
    model_layout.json              モデルの一覧（personのみ）と重みアドレス
  src/
    main.cpp                     usermain(): 割り込み登録、チャネル作成、初期化タスク起動
    app_config.hpp               優先度、スタック、キュー深さ
    app_context.hpp              タスクが共有する資源（AppContext）
    task/initialize_task.*       ドライバ初期化→カメラ/推論タスク起動
    task/camera_task.*           Pipe1表示、Pipe2の転送、結果の描画
    task/inference_task.*        NPU実行→後処理→結果の送信
    task/channels.hpp            フレームと結果のチャネル
    model/person_network.c       生成されたネットワークのCラッパ
    model/person_model.*         NpuNetworkアダプタ
    model/person_decoder.*       YOLOX後処理→BoxSet
```

## 動かす

```sh
make -C userspace/mini-ai-app setup      # 依存確認、personモデル生成、CubeMX生成、CMake構成
make -C userspace/mini-ai-app monitor    # 別端末でUART
make -C userspace/mini-ai-app ai-load    # 初回とモデル変更時。重みとblobをNORへ
make -C userspace/mini-ai-app ram-run    # ビルドしてRAMで実行
```

UARTに`camera: pipe1=started pipe2=started`、続いて`mini: model ready`と`mini: first inference`が出れば動作しています。1秒ごとに`mini: inference/s=...`が出ます。

モデルの生成物は`userspace/ai-app/models/person/`を使います（`AI_MODELS_DIR`）。ai-appで既に`make ai-models`を済ませていれば再生成は不要です。
