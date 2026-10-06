# ui_designer

ai-appの画面に出すボタンなどのレイアウトを編集し、実機用のC++ヘッダを生成するツールです。標準ライブラリだけで動き、追加パッケージは不要です（Python 3.10以上）。

```text
  ui_layout.json ──serve──> ブラウザで編集 ──Save──> ui_layout.json
        │
        ├──render───> preview.png（実機と同じ5x7フォント、RGB565の色）
        └──generate─> ui_layout.hpp（kernel/middleware/ui の ButtonSpec 表）
```

## 使い方

```sh
# ブラウザのエディタを開く（127.0.0.1:8765 にだけ待ち受けます）
make -C userspace/ai-app ui-designer
# ヘッダを再生成する
make -C userspace/ai-app ui-layout
# 整合性の確認（CIなどで）: フォント表の一致と、ヘッダが最新かどうか
make -C userspace/ai-app ui-layout-check
```

直接呼ぶ場合は次のとおりです。

```sh
python3 host_app/ui_designer validate --layout userspace/ai-app/config/ui_layout.json --check-font
python3 host_app/ui_designer render   --layout userspace/ai-app/config/ui_layout.json --output /tmp/preview.png --pressed toggle_boxes
python3 host_app/ui_designer generate --layout userspace/ai-app/config/ui_layout.json --output userspace/ai-app/src/ui/ui_layout.hpp
python3 host_app/ui_designer serve    --layout userspace/ai-app/config/ui_layout.json --port 8765
```

## CLIで編集する

ブラウザを使わずにレイアウトを変更できます。各コマンドは保存前に検証し、不正なら書き込まずに終了コード2を返します。`--header PATH`を付けると保存後にC++ヘッダも再生成します。

```sh
L=userspace/ai-app/config/ui_layout.json
H=userspace/ai-app/src/ui/ui_layout.hpp
python3 host_app/ui_designer init   --layout $L                       # 空のレイアウトを作る（--width/--height/--namespace）
python3 host_app/ui_designer list   --layout $L                       # 一覧（--jsonで整形済みJSON）
python3 host_app/ui_designer add    --layout $L --id start --x 16 --y 392 --width 160 --height 72 \
    --label START --fill '#00AA00' --on-tap OnStartTap --header $H
python3 host_app/ui_designer set    --layout $L --id start --x 24 --text-scale 4 --on-press OnStartDown --header $H
python3 host_app/ui_designer set    --layout $L --id start --rename run --on-tap '' --header $H   # 改名、コールバック解除
python3 host_app/ui_designer remove --layout $L --id run --header $H
python3 host_app/ui_designer screen --layout $L --namespace demo::ui
```

`add`と`set`のオプションは共通です。`--label`、`--x`、`--y`、`--width`、`--height`、スタイル（`--fill`、`--pressed-fill`、`--border`、`--text`、`--text-scale`、`--border-width`）、コールバック（`--on-tap`、`--on-press`）。`add`で`--label`を省くと`id`を大文字にしたものになります。

## コールバック

ウィジェットごとに`on_tap`（押して離した）と`on_press`（押した瞬間）にC++のメソッド名を割り当てられます。生成ヘッダには次のテンプレート関数が入ります。

```cpp
template <typename Handlers>
bool Dispatch(Handlers &handlers, const ui::Event &event);   // 呼んだらtrue
```

アプリ側は、割り当てた名前のメソッド`void Name(const ui::Event &)`を持つ任意の型を渡します。名前が足りなければコンパイルエラーになるので、レイアウトを変えたときの実装漏れをビルドで検出できます。仮想関数は使いません。

```cpp
struct UiHandlers {
    bool &show_boxes;
    void OnToggleBoxesTap(const ui::Event &) { show_boxes = !show_boxes; }
} handlers{show_boxes};

const ui::Event event = panel.Update(sample);
if (!app_ui::Dispatch(handlers, event)) { /* 割り当てのないイベント */ }
```

## エディタ

- 800x480のキャンバスにボタンを置き、ドラッグで移動、右下の角で大きさを変えます。方向キーで1 px、Shift付きで8 px動かせます。
- 右の欄で`id`、`label`、位置、大きさ、色、文字倍率、枠の太さ、コールバック（`on_tap`、`on_press`）を編集します。
- 文字は実機と同じグリフ表（`/api/font`）で描くので、見た目は実機と一致します。`pressed preview`で押下時の色を確認できます。
- 変更するたびにサーバ側で検証し、生成されるC++をその場で表示します。画面外、重なり、`id`の重複、フォントにない文字はエラーになります。
- `Save`で`ui_layout.json`に書き戻します。書き込む先はコマンドラインで渡したファイルだけです。

## レイアウトファイル

```json
{
  "schema_version": 1,
  "screen": {"width": 800, "height": 480},
  "namespace": "uai::ai::app_ui",
  "widgets": [
    {
      "type": "button",
      "id": "toggle_boxes",
      "label": "BOXES",
      "x": 624, "y": 392, "width": 160, "height": 72,
      "on_tap": "OnToggleBoxesTap",
      "style": {
        "fill": "#2060C0", "pressed_fill": "#103060",
        "border": "#FFFFFF", "text": "#FFFFFF",
        "text_scale": 3, "border_width": 2
      }
    }
  ]
}
```

| 項目 | 内容 |
| --- | --- |
| `screen` | 画面の大きさ。ウィジェットはこの中に収まる必要があります |
| `namespace` | 生成するヘッダのC++名前空間 |
| `widgets[].type` | 現在は`button`のみ |
| `widgets[].id` | `[a-z][a-z0-9_]*`。`WidgetId::kToggleBoxes`のようにPascalCaseの列挙子になります。数値は並び順で1から振ります |
| `widgets[].label` | フォントにある文字（英大文字、数字、空白、`- + . : / %`）。小文字は大文字として描かれます |
| `widgets[].style` | 省略した項目は既定値になります |
| `widgets[].on_tap`、`widgets[].on_press` | 省略可。`Dispatch()`が呼ぶメソッド名（C++識別子） |

## 生成されるヘッダ

`namespace`の中に`kScreenWidth`、`kScreenHeight`、`enum class WidgetId`、`constexpr ui::ButtonSpec kButtons[]`、`kButtonCount`、`Dispatch()`を出します。実機では`ui::ButtonPanel panel(kButtons, kButtonCount)`として使います（[docs/middleware/ui.md](../../docs/middleware/ui.md)）。ヘッダは生成物ですがリポジトリに入れており、`generate --check`で最新かどうかを確認できます。

## 今後の拡張の方針

ウィジェットの種類を増やすときは、(1) `kernel/middleware/ui`に型と`Paint`/`Update`を足し、(2) `schema.py`で検証、(3) `render.py`と`static/index.html`の描画、(4) `emit_cpp.py`の出力を揃えます。候補は次のとおりです。

| 機能 | ねらい |
| --- | --- |
| ラベル（静的な文字、数値の表示枠） | FPSや検出数などの状態表示。実行時に文字列を差し替えるスロットを生成 |
| トグル／ラジオボタン | モデル切り替え（person/face/segmentation）など排他選択 |
| スライダー | しきい値の調整。値域と刻みを定義から生成 |
| 画面（複数レイアウト）と遷移 | 設定画面とプレビュー画面の切り替え |
| カメラ画像のPNGを背景に読み込む | 実際の映像との重なりを見ながら配置 |
| フォントの追加（サイズ、半角記号） | 表の単一ソース化（`canvas.cpp`の生成も`ui_designer`で行う） |

## テスト

```sh
python3 -m unittest discover -s host_app/ui_designer/tests -t host_app
```

検証（画面外、重なり、`id`、グリフのない文字、コールバック名）、`canvas.cpp`とのフォント表の一致、描画のピクセル、生成ヘッダの内容と`Dispatch()`の分岐、リポジトリ内のヘッダが最新であること、CLIの編集コマンドと`--check`を確認します。
