# バイナリの測定・外部 NOR 配置

`plan_model_flash.py` は任意のバイナリのファイル長を測定し、予約領域と固定アドレスとの重複・領域超過・消去単位を検査して JSON と SVG を出力します。Python 標準ライブラリだけを使用し、モデルのパース・再生成・書き込みはしません。

生成済みの３モデルを `make APP_TARGET=sample-ai2 BUILD_DIR=build ai-models` と `make APP_TARGET=sample-ai2 BUILD_DIR=build build` で準備した後、リポジトリのルートから次を実行します。

```sh
python3 userspace/sample-ai2/tools/plan_model_flash.py \
  userspace/sample-ai2/tools/flash_layout.example.json \
  --json build/sample-ai2-layout.json --svg build/sample-ai2-layout.svg
```

マニフェスト内のバイナリパスはマニフェストからの相対パス（または絶対パス）。`region` の `start`, `size`, `erase_size`, `alignment` を指定し、`artifacts` の各項目で `name`, `path`, 任意の `address` / `alignment` を指定します。`reserved` は `name`, `address`, `size` を指定する任意のリストです。アドレスは数値か `0x` で始まる文字列です。`address` のあるバイナリは移動しません。アドレスなしのバイナリは消去単位で切り上げ、サイズ降順の first-fit で空き位置を**提案**します。最適解の保証ではありません。

実際の重みのアドレスは生成済み STAI ネットワークに埋め込まれ、command blob はリンカで固定配置されています。提案を反映するには、重みは `AI_MODEL_NETWORK_ADDRESS` を設定してモデルを**再生成**し、command blob は sample-ai2 の CMake の `--section-start` とマニフェストを共に更新し、再ビルド・測定・実機検証が必要です。ツールの出力 JSON/SVG をそのまま STM32CubeProgrammer へ渡してはいけません。異なるビルドディレクトリを使う場合はマニフェストのパスも変更してください。

`sh userspace/sample-ai2/tools/tests/run.sh` で Python の配置テストとネイティブ C++ middleware テストを実行できます。
