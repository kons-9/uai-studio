# ai-appのサードパーティ部品

| 部品 | 扱い |
| --- | --- |
| `vision_models_pp/` | STのvision-models post-processing。ビルドに必要なソースだけを同梱しています。`od_pp_st_yolox.c`には`max_boxes_limit`を超える書き込みを防ぐ修正を入れています。 |
| STEdgeAI Neural-ARTランタイム | 同梱しません。`STEDGEAI_LIB_DIR`で`Middlewares/ST/AI`を指定します。CMakeが`ll_aton`の版を確認し、対応するCM55 GCCアーカイブを選びます。 |

依存関係は通常のCMake構成時に確認します。`STEDGEAI_LIB_DIR`と、外部の後処理ソースを使う場合は`AI_VISION_MODELS_PP_DIR`をホスト設定へ指定してください。
