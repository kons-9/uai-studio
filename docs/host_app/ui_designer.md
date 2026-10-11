# ui_designer

## 概要

`host_app/ui_designer`は、LCDに表示する複数画面のウィジェット配置を編集し、プレビューPNGと実機用C++ヘッダを生成するツールです。ブラウザのエディタとCLIの編集コマンドがあり、Python 3.10以上の標準ライブラリだけで動作します。編集・生成に実機は不要です。

生成物は[kernel/middlewareのui](../middleware/ui.md)で描画・タッチ処理に使用します。レイアウトの検証とコード生成を担当するツールであり、コールバックの処理や機能・HWの状態変更はアプリ側で実装します。

### 対応ウィジェット

| 種類 | 用途 | 主なイベント |
| --- | --- | --- |
| `button` | 文字・アイコンのボタン、画面遷移 | `on_tap`、`on_press` |
| `label` | 固定文字の表示 | なし |
| `slider` | 範囲内の値をスライダーで変更 | `on_change` |
| `dial` | 範囲内の値をダイヤルで変更 | `on_change` |
| `wheel` | 選択肢から項目を選ぶ | `on_change` |
| `number` | 単位・小数桁付きの数値表示 | なし |
| `image` | PNGをRGB565画像として表示 | なし |
| `pad` | 十字パッドと回転操作 | `on_tap`、`on_press`、`on_change` |

### 入力と生成物

入力は表示寸法、名前空間、画面、ウィジェットを定義したレイアウトJSONです。ai-appの入力は`userspace/ai-app/config/ui_layout.json`です。画像はこのJSONからの相対パスで参照します。

| 操作 | 出力 |
| --- | --- |
| エディタのSave / CLI編集 | レイアウトJSONを書き戻す |
| `render` | 画面ごとのプレビューPNG |
| `generate` | 画面・部品のIDと配置表、`Dispatch()`を含むC++ヘッダ |
| 画像を含む`generate` | 上記に加えて`<ヘッダ名>_images.hpp`にRGB565の画像配列を生成 |

## 使い方

コマンドはリポジトリルートで実行します。

### ブラウザで編集する

```sh
python3 host_app/ui_designer serve \
  --layout userspace/ai-app/config/ui_layout.json --port 8765
```

既定URLは`http://127.0.0.1:8765/`です。ブラウザの自動起動を止めるには`--no-browser`を指定します。ポート競合時は`--port 8767`などに変更し、リモート環境ではポートを転送してください。サーバーはlocalhost専用です。

1. 上部の画面タブで編集対象を選びます。Screen pageでIDと背景を変更し、+ Screen / Remove screenで画面を追加・削除します。
2. 部品を追加し、ドラッグで移動、右下の角でサイズ変更します。
3. Propertiesで種類に応じた値、色、ラベル、コールバック、画面遷移先を設定します。
4. 検証結果と生成されるC++を確認し、Saveで指定したレイアウトJSONへ保存します。

変更のたびにサーバー側で検証します。押下時・選択時の色はpressed / checkedで確認できます。ブラウザの輪郭描画には実機とわずかな差があるため、厳密な確認にはCLIのプレビューPNGを使います。画像のプレビューは保存済みJSONから変換されるため、`source`を変更したらSave後に確認してください。

### 検証とプレビューを行う

```sh
python3 host_app/ui_designer validate \
  --layout userspace/ai-app/config/ui_layout.json --check-font
python3 host_app/ui_designer render \
  --layout userspace/ai-app/config/ui_layout.json \
  --screen main --output /tmp/ui-main.png
```

`--check-font`は実機のフォント表との一致も確認します。`render`では`--screen`で画面を選び、`--checked ID`、`--pressed ID`でボタンの状態を指定できます。色はRGB565、文字は実機と同じ5x7フォントで描画します。

### ヘッダを生成する

ソースを変更せず生成を試す例です。

```sh
python3 host_app/ui_designer generate \
  --layout userspace/ai-app/config/ui_layout.json \
  --output /tmp/ui_layout.hpp
python3 host_app/ui_designer generate \
  --layout userspace/ai-app/config/ui_layout.json \
  --output /tmp/ui_layout.hpp --check
```

`--check`は書き込まずに生成結果と既存ヘッダを比較し、不一致なら終了コード1を返します。画像がある場合は画像ヘッダも比較します。`--format`を付けると`clang-format-21`で整形します。整形したヘッダの比較にも同じオプションを指定してください。

ai-appの作業にはMakeターゲットもあります。

```sh
make -C userspace/ai-app ui-designer
make -C userspace/ai-app ui-layout
make -C userspace/ai-app ui-layout-check
```

`ui-layout`はアプリのヘッダを再生成・整形し、`ui-layout-check`はフォントと生成ヘッダが最新かを確認します。これらの整形・比較にはclang-format 21が必要です。

### CLIだけでレイアウトを作る

次の例は`/tmp`に新規レイアウトを作り、メニューへの遷移ボタンとスライダーを追加します。

```sh
python3 host_app/ui_designer init --layout /tmp/uai-ui-example.json
python3 host_app/ui_designer screen-add \
  --layout /tmp/uai-ui-example.json --id settings --background '#101820'
python3 host_app/ui_designer add \
  --layout /tmp/uai-ui-example.json --id open_settings \
  --x 744 --y 0 --width 56 --height 48 --icon menu --navigate settings
python3 host_app/ui_designer add \
  --layout /tmp/uai-ui-example.json --screen settings --type slider --id level \
  --x 40 --y 120 --width 720 --height 72 \
  --min 0 --max 100 --step 5 --value 50 \
  --label 'LEVEL %' --on-change OnLevelChange
python3 host_app/ui_designer list --layout /tmp/uai-ui-example.json --json
python3 host_app/ui_designer generate \
  --layout /tmp/uai-ui-example.json --output /tmp/uai-ui-example.hpp
```

`set`は部品の変更・別画面への移動、`remove`は部品の削除、`screen-set`は画面名や背景の変更、`screen-remove`は画面の削除に使います。編集コマンドは保存前に検証し、不正なら書き込まず終了コード2を返します。`--header PATH`を付けると保存後にヘッダも再生成します。`init`は新規作成用なので、既存レイアウトを編集する場合は使わないでください。

### 機能IDを参照する

`--feature-catalog`で機能IDと操作IDの一覧を指定すると、ウィジェットの`feature`と`operation`を検証・編集できます。ai-appの既定レイアウトには`ui_feature_catalog.json`が自動適用されます。別のレイアウトへ機能参照を付ける場合は、カタログを明示してください。

`operation`は同じ`feature`に宣言されている必要があります。カタログなしの参照や未知のIDは拒否します。参照がある場合、生成ヘッダに`kFeatureBindings`を追加しますが、操作の意味やHW制御は生成しません。

## 実機側で使う

生成ヘッダには`ScreenId`、`WidgetId`、各種類の配置表、`kScreens`と`Dispatch()`を出力します。アプリ側では次のテンプレート関数へハンドラーを渡します。

```cpp
template <typename Handlers>
bool Dispatch(Handlers &handlers, const ui::Event &event);
```

ハンドラーに、指定したコールバック名の`void Name(const ui::Event &)`を実装します。ボタンの`navigate`を使う場合は`void ShowScreen(ScreenId)`も必要です。`on_tap`と`navigate`を同時に指定したときは、コールバックの後に画面遷移します。

画像配列には`.ui_assets`セクションを指定します。独自のリンカスクリプトでも対応する出力セクションを用意してください。描画への組み込みは[ui](../middleware/ui.md)を参照してください。

## 制約と診断

- 部品は表示領域に収まり、同じ画面内で重ならない必要があります。IDは全画面で一意、`navigate`先は存在する画面でなければなりません。
- 文字は実機フォントの英大文字、数字、空白、`- + . : / %`を使います。小文字は大文字で描かれます。日本語フォントはありません。
- slider / dialは`min < max`、`1 <= step <= max - min`、値は範囲内です。wheelの選択肢は1〜32件です。
- numberの`value`は`10^decimals`倍した整数です。例えば`decimals=1`、`value=1234`は`123.4`を表示します。
- 画像は対応する8bit PNGをJSONからの相対パスで指定し、`/`や`..`で始まるパスは使えません。変換後の画像1枚は`width * height * 2 <= 128 KiB`です。
- padは32x32以上、1画面に最大2個です。
- 旧`schema_version: 1`はカメラ背景のmain画面として読めます。編集コマンドで保存するとversion 2へ更新します。

詳細なヘルプは`python3 host_app/ui_designer <サブコマンド> --help`、全オプションは[ツールのREADME](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md)で確認できます。