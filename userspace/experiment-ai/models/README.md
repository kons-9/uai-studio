# experiment-aiのモデル生成

生成スクリプトの使い方、取得元、生成オプション、出力ファイルはai-appと同じです。[ai-appのmodels/README.md](../../ai-app/models/README.md)を参照し、パスを`userspace/experiment-ai/models/`に読み替えてください。

```sh
sh userspace/experiment-ai/models/generate_model.sh person
sh userspace/experiment-ai/models/generate_model.sh segmentation
sh userspace/experiment-ai/models/generate_model.sh face
```

| モデル | 重みのアドレス | command blobのアドレス | command blobのファイル |
| --- | --- | --- | --- |
| person | `0x70380000` | `0x70500000` | `network_blobs_person.hex` |
| segmentation | `0x70600000` | `0x70560000` | `network_blobs_segmentation.hex` |
| face | `0x70800000` | `0x70580000` | `network_blobs_face.hex` |
