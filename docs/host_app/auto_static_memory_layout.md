# auto_static_memory_layout

## 概要

`host_app/auto_static_memory_layout`は、ai-appの物理メモリ設定、バッファ要求、生成済みモデルを読み込み、静的メモリ配置を解決するツールです。領域の重複・容量超過などを検証し、C++ヘッダとリンカスクリプトを生成します。Python標準ライブラリだけで動作し、実機は不要です。

ai-appの通常ビルドではCMakeから自動実行されます。手動CLIは設定の検証や生成先の変更、ブラウザGUIはバッファ寸法や予約領域の編集に利用します。実機側での配置の参照方法は[memory](../middleware/memory.md)を参照してください。

## 入力と生成物

| 入力 | 内容 |
| --- | --- |
| `board_memory.json` | 物理メモリ領域、モデル重みのアドレス、command blobのセクションなど |
| `application_memory.json` | キャプチャ・表示・推論などのバッファ寸法、個数、アラインメント、固定予約領域 |
| `model_layout.json` | 配置対象のモデルとその順序 |
| 生成済みモデルのディレクトリ | `stai_network.h`のテンソル情報、`network.c`、`network_ecblobs.h`、重みなど |
| ベースのリンカスクリプト | 生成するリンカスクリプトの土台 |

入力JSONの既定の配置場所は`userspace/ai-app/config/`、モデルは`userspace/ai-app/models/`です。モデルが未生成の場合は[モデルの準備](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/models/README.md)を先に行ってください。このツール自体はモデルを生成しません。

解決済みJSONを中間表現として、次の生成物を出力します。

| 生成物 | 用途 |
| --- | --- |
| `memory_layout.json` | 解決済み配置。後段の生成コマンドの入力 |
| `memory_layout.yml` | 解決済み配置のYAML表現 |
| `key.hpp` | 静的メモリ配置のキー |
| `raw.hpp` | 静的メモリ配置の宣言 |
| `memory_config.hpp` | 実機側で利用するメモリ設定 |
| リンカスクリプト | 解決した配置を最終リンクに反映 |

## 使い方

コマンドはリポジトリルートで実行します。CLIの出力先は必ず引数で指定します。入力とは別の出力先を使うことで、ソースツリーを変更せず試せます。

### 一括で解決・生成する

```sh
python3 host_app/auto_static_memory_layout all \
  --board userspace/ai-app/config/board_memory.json \
  --application userspace/ai-app/config/application_memory.json \
  --models-dir userspace/ai-app/models \
  --model-config userspace/ai-app/config/model_layout.json \
  --output-dir /tmp/ai-app-memory-layout \
  --linker-base userspace/ai-app/stm32n6570-dk-npu-ram.ld
```

成功時の出力構成は次のとおりです。

```text
/tmp/ai-app-memory-layout/
  memory_layout.json
  memory_layout.yml
  stm32n6570-dk-npu-ram.ld
  middleware/memory/generated/
    memory_config.hpp
    static_memory_layout/
      key.hpp
      raw.hpp
```

### 配置の解決だけを行う

```sh
python3 host_app/auto_static_memory_layout resolve \
  --board userspace/ai-app/config/board_memory.json \
  --application userspace/ai-app/config/application_memory.json \
  --models-dir userspace/ai-app/models \
  --model-config userspace/ai-app/config/model_layout.json \
  --output /tmp/ai-app-memory-layout/memory_layout.json
```

解決済みJSONからYAMLだけを作る場合は次のように実行します。

```sh
python3 host_app/auto_static_memory_layout generate_yml \
  --input /tmp/ai-app-memory-layout/memory_layout.json \
  --output /tmp/ai-app-memory-layout/memory_layout.yml
```

### サブコマンド

| コマンド | 処理 | 出力先の指定 |
| --- | --- | --- |
| `resolve` | ボード・アプリ・モデルの入力から配置を解決 | `--output` |
| `generate_yml` | 解決済みJSONをYAMLに変換 | `--output` |
| `generate_cpp` | 解決済みJSONからC++ヘッダとリンカスクリプトを生成 | `--key-header`、`--raw-header`、`--memory-config`、`--linker` |
| `all` | 解決と全生成を一括実行 | `--output-dir` |
| `gui` | ブラウザで入力設定を編集・検証 | 成果物をブラウザからダウンロード |

`generate_cpp`には`--input`と`--linker-base`も必要です。出力ファイル名や配置場所を個別に指定したい場合に使います。詳細は`python3 host_app/auto_static_memory_layout <サブコマンド> --help`で確認できます。

### ブラウザで編集する

```sh
python3 host_app/auto_static_memory_layout gui --open-browser
```

既定URLは`http://127.0.0.1:8766/`です。`--open-browser`を省略すると自動でブラウザを開きません。ポート競合時は`--port 8767`、空きポートの自動選択には`--port 0`を指定します。リモート環境では表示されたポートを転送してください。localhost以外には公開しません。

GUIの入力も既定ではai-appです。`--board`、`--application`、`--models-dir`、`--model-config`、`--linker-base`で変更できます。

1. Runtimeで寸法、ピクセル当たりのバイト数、バッファ数、アラインメントを編集します。
2. Reservations / Regionsで予約と物理メモリ領域を編集します。領域マップから予約を選ぶと対応する詳細行を選択できます。
3. ValidateでCLIと同じ配置解決を行い、エラーを確認します。編集後も自動検証されます。
4. Export ZIPで編集済みの入力JSON、解決済みJSON/YAML、ヘッダ3つ、リンカスクリプトをダウンロードします。

JSONタブでは入力の全項目を編集でき、JSONのインポート・エクスポートも行えます。予約の追加・削除もこのタブで行います。

編集内容はブラウザ内のドラフトです。入力ファイルやビルド出力は書き換えません。Reloadは入力を再読込してドラフトを破棄し、別のポートやブラウザにはドラフトを引き継ぎません。生成物を採用する場合は、ダウンロードした設定と成果物を確認してからアプリへ反映します。

## 制約と診断

- 配置の解決とZIP出力には生成済みモデルが必要です。不足時も設定の編集とJSON単体の出力はできますが、使用量は未計算になります。
- GUIから参照するモデル生成物はリポジトリ内に配置してください。
- メモリマップは静的予約の容量を表示します。コード、モデル重み、command blobの使用量は含みません。
- 予約のオフセットは予約領域内の相対値です。最終アドレスはリンカが決定します。
- セクション名を変更するときは、対応するセクションを持つベースのリンカスクリプトも必要です。
- CLIの入力読込や配置解決のエラーは標準エラーへ出力され、終了コード2を返します。