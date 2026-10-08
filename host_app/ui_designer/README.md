# ui_designer

ai-appの画面（複数ページ）に出すボタン、ラベル、スライダー、ダイヤル、ホイール、数値表示、画像、十字パッドのレイアウトを編集し、実機用のC++ヘッダを生成するツールです。標準ライブラリだけで動き、追加パッケージは不要です（Python 3.10以上。PNGの読み書きも自前です）。

```text
  ui_layout.json ──serve──> ブラウザで編集 ──Save──> ui_layout.json
        │
        ├──render───> preview.png（画面ごと。実機と同じ5x7フォント、RGB565の色）
        └──generate─> ui_layout.hpp（kernel/middleware/ui の ScreenSpec / ButtonSpec / LabelSpec / SliderSpec / DialSpec / WheelSpec / NumberSpec / ImageSpec / PadSpec 表と Dispatch）
                      ui_layout_images.hpp（imageウィジェットがあるとき。PNGを変換したRGB565の配列）
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
L=userspace/ai-app/config/ui_layout.json
python3 host_app/ui_designer validate --layout $L --check-font
python3 host_app/ui_designer render   --layout $L --output /tmp/main.png --checked person --pressed face
python3 host_app/ui_designer render   --layout $L --output /tmp/menu.png --screen menu
python3 host_app/ui_designer generate --layout $L --output userspace/ai-app/src/ui/ui_layout.hpp
python3 host_app/ui_designer serve    --layout $L --port 8765
```

## CLIで編集する

ブラウザを使わずにレイアウトを変更できます。各コマンドは保存前に検証し、不正なら書き込まずに終了コード2を返します。`--header PATH`を付けると保存後にC++ヘッダも再生成します。

```sh
L=userspace/ai-app/config/ui_layout.json
H=userspace/ai-app/src/ui/ui_layout.hpp
python3 host_app/ui_designer init          --layout $L                 # カメラ画面mainだけのレイアウト
python3 host_app/ui_designer list          --layout $L                 # 画面ごとの一覧（--jsonで整形済みJSON）
python3 host_app/ui_designer screen-add    --layout $L --id menu --background '#101820'
python3 host_app/ui_designer screen-set    --layout $L --id menu --rename settings   # navigate参照も追従
python3 host_app/ui_designer screen-remove --layout $L --id settings --force
python3 host_app/ui_designer add --layout $L --id open_menu --x 744 --y 0 --width 56 --height 48 \
    --icon menu --navigate menu --header $H                            # ハンバーガーで画面遷移
python3 host_app/ui_designer add --layout $L --screen menu --type slider --id level \
    --x 40 --y 120 --width 720 --height 72 --min 0 --max 100 --step 5 --value 50 \
    --label 'LEVEL %' --on-change OnLevelChange --header $H
python3 host_app/ui_designer add --layout $L --screen menu --type label --id title \
    --x 96 --y 24 --width 400 --height 40 --text SETTINGS --text-scale 4 --fill none
python3 host_app/ui_designer add --layout $L --screen menu --type dial --id period \
    --x 560 --y 104 --width 200 --height 220 --min 100 --max 2000 --step 100 --value 500 \
    --label 'STATUS MS' --on-change OnStatusPeriodChange
python3 host_app/ui_designer add --layout $L --screen menu --type wheel --id models \
    --x 40 --y 224 --width 240 --height 150 --items ALL,PERSON,FACE --value 0 --on-change OnModelsChange
python3 host_app/ui_designer add --layout $L --type number --id detections \
    --x 600 --y 72 --width 188 --height 56 --label DET --decimals 0 --fill none
python3 host_app/ui_designer add --layout $L --screen menu --type image --id logo \
    --x 670 --y 16 --width 114 --height 54 --source ui/logo.png --header $H     # PNGはレイアウトからの相対パス
python3 host_app/ui_designer add --layout $L --id round --x 732 --y 8 --width 56 --height 56 \
    --shape ellipse --icon menu --navigate menu                                  # 丸ボタン
python3 host_app/ui_designer add --layout $L --id next --x 392 --y 308 --width 56 --height 56 \
    --shape triangle_right --label '' --on-tap OnNextTap                        # 矢印キー
python3 host_app/ui_designer add --layout $L --type pad --id nav \
    --x 620 --y 144 --width 160 --height 160 --on-tap OnNavTap --on-change OnNavRotate  # 十字パッド
python3 host_app/ui_designer set --layout $L --id level --screen main --value 30            # 別画面へ移動、値変更
python3 host_app/ui_designer set --layout $L --id open_menu --navigate ''                   # 遷移を解除
python3 host_app/ui_designer remove --layout $L --id level --header $H
python3 host_app/ui_designer screen --layout $L --namespace demo::ui                        # 表示サイズと名前空間
```

`add`と`set`のオプションは共通です。

| 対象 | オプション |
| --- | --- |
| 共通 | `--x`、`--y`、`--width`、`--height`、`--text-color`、`--text-scale`、`--screen`（addは配置先、setは移動先） |
| button | `--label`、`--icon`（none/menu/back/close）、`--shape`（rectangle/rounded/pill/ellipse/triangle_up/triangle_down/triangle_left/triangle_right/diamond）、`--navigate SCREEN_ID`、`--fill`、`--pressed-fill`、`--checked-fill`、`--border`、`--border-width`、`--on-tap`、`--on-press` |
| label | `--text`、`--fill`（`none`で透過）、`--align`、`--padding` |
| slider | `--label`、`--min`、`--max`、`--step`、`--value`、`--track`、`--fill`、`--knob`、`--show-value`、`--on-change` |
| dial | `--label`、`--min`、`--max`、`--step`、`--value`、`--face`、`--track`、`--fill`、`--pointer`、`--show-value`、`--on-change` |
| wheel | `--items A,B,C`、`--value`（選択中の添字）、`--fill`、`--highlight`、`--selected-text`、`--border`、`--on-change` |
| number | `--label`、`--unit`、`--decimals`、`--value`（`10^decimals`倍の整数）、`--fill`（`none`で透過）、`--align` |
| image | `--source PATH`（レイアウトファイルからの相対パス。`/`や`..`で始まるものは不可）、`--transparent #RRGGBB`（`none`で自動） |
| pad | `--center true/false`、`--fill`、`--pressed-fill`、`--center-fill`、`--border`、`--arrow`、`--border-width`、`--on-tap`、`--on-press`（`Event::value`は象限 0上 1右 2下 3左 4中央）、`--on-change`（リングを回したときに±1） |

種類に合わないオプションはエラーになります。`add`で文字を省くと`id`を大文字にして`_`を空白にしたものになります。

## コールバックと画面遷移

ボタンとパッドには`on_tap`（押して離した）と`on_press`（押した瞬間）、スライダー・ダイヤル・ホイール・パッドには`on_change`（値が変わった。`Event::value`はスライダー・ダイヤルなら値、ホイールなら選択した添字、パッドなら回転方向±1）にC++のメソッド名を割り当てられます。パッドの`on_tap`/`on_press`では`Event::value`が押した象限（`ui::PadSegment`）です。数値表示と画像にイベントはありません。ボタンの`navigate`に画面idを書くと、タップで`ShowScreen(ScreenId)`が呼ばれます（`on_tap`があれば先に呼びます）。生成ヘッダには次のテンプレート関数が入ります。

```cpp
template <typename Handlers>
bool Dispatch(Handlers &handlers, const ui::Event &event);   // 呼んだらtrue
```

アプリ側は、割り当てた名前のメソッド`void Name(const ui::Event &)`と、`navigate`を使う場合は`void ShowScreen(ScreenId)`を持つ任意の型を渡します。名前が足りなければコンパイルエラーになるので、レイアウトを変えたときの実装漏れをビルドで検出できます。仮想関数は使いません。ai-appでは[src/ui/app_ui.hpp](../../userspace/ai-app/src/ui/app_ui.hpp)の`AppUi`がこの型で、`HandleTouch()`の中で`Dispatch(*this, event)`を呼びます。

## エディタ

- 上部のタブで画面を切り替えます。「Screen page」でid、背景（camera / 単色）を変え、`+ Screen` / `Remove screen`で画面を増減します。
- `+ Button` / `+ Label` / `+ Slider` / `+ Dial` / `+ Wheel` / `+ Number` / `+ Image` / `+ Pad`で現在の画面に部品を置き、ドラッグで移動、右下の角で大きさを変えます。方向キーで1 px、Shift付きで8 px動かせます。
- 右の欄で種類に応じた項目を編集します。ボタンは`label`または`icon`、`shape`、`navigate`、色、コールバック。ラベルは`text`、文字色、背景色（`none`で透過）、`align`、`padding`。スライダー・ダイヤルは`min`/`max`/`step`/`value`と色、`show_value`、`on_change`。ホイールは`items`（カンマ区切り）と`value`。数値は`label`、`unit`、`decimals`、`value`。画像は`source`と`transparent`（`auto`ならPNGのアルファから透過色を決めます）。パッドは`center`と色、コールバック。
- 文字は実機と同じグリフ表（`/api/font`）、アイコン・スライダー・ダイヤル・ホイール・数値も実機と同じ幾何で描くので、見た目は実機と一致します（丸ボタンや角丸・三角の輪郭とダイヤルの弧はブラウザの描画なので縁が少し違います。厳密な確認は`preview.png`で）。画像は保存済みレイアウトから変換したビットマップ（`/api/image.png?id=`）を表示し、未保存の`source`は紫の枠で示します。`pressed`と`checked`で選択中のボタンの押下時・選択時の色を確認できます。
- 変更するたびにサーバ側で検証し、生成されるC++をその場で表示します。画面外、同じ画面内の重なり、`id`の重複、存在しない`navigate`先、フォントにない文字はエラーになります。
- `Save`で`ui_layout.json`に書き戻します。書き込む先はコマンドラインで渡したファイルだけです。

## レイアウトファイル

```json
{
  "schema_version": 2,
  "screen": {"width": 800, "height": 480},
  "namespace": "uai::ai::app_ui",
  "screens": [
    {
      "id": "main",
      "background": "camera",
      "widgets": [
        {"type": "button", "id": "open_menu", "label": "", "icon": "menu", "navigate": "menu",
         "x": 744, "y": 0, "width": 56, "height": 48,
         "style": {"fill": "#000000", "pressed_fill": "#404040", "checked_fill": "#000000",
                   "border": "#000000", "text": "#FFFFFF", "text_scale": 2, "border_width": 0}},
        {"type": "label", "id": "status", "text": "AI STARTING",
         "x": 0, "y": 0, "width": 736, "height": 24,
         "style": {"text": "#FFFFFF", "fill": "#000000", "text_scale": 2, "align": "left", "padding": 4}}
      ]
    },
    {
      "id": "menu",
      "background": "#101820",
      "widgets": [
        {"type": "slider", "id": "min_confidence", "label": "MIN CONFIDENCE %", "on_change": "OnMinConfidenceChange",
         "x": 40, "y": 120, "width": 720, "height": 72, "min": 0, "max": 100, "step": 5, "value": 50,
         "style": {"track": "#304050", "fill": "#2060C0", "knob": "#FFFFFF", "text": "#FFFFFF",
                   "text_scale": 2, "show_value": true}}
      ]
    }
  ]
}
```

| 項目 | 内容 |
| --- | --- |
| `screen` | 表示の大きさ。ウィジェットはこの中に収まり、同じ画面内で互いに重ならない必要があります |
| `namespace` | 生成するヘッダのC++名前空間 |
| `screens[].id` | `[a-z][a-z0-9_]*`。`ScreenId::kMenu`のようにPascalCaseの列挙子になります。並び順で0から振り、先頭が起動時の画面です |
| `screens[].background` | `camera`（Pipe1の映像の上に描く）または`#RRGGBB`（塗りつぶし） |
| `widgets[].type` | `button`、`label`、`slider`、`dial`、`wheel`、`number`、`image`、`pad` |
| `widgets[].id` | 全画面で一意。`WidgetId::kOpenMenu`のようにPascalCaseの列挙子になります。数値は全画面の並び順で1から振ります |
| `widgets[].label`（button/slider/dial/number）、`widgets[].text`（label）、`widgets[].items`（wheel）、`widgets[].unit`（number） | フォントにある文字（英大文字、数字、空白、`- + . : / %`）。小文字は大文字として描かれます。ラベルの`text`は初期値で63文字まで、ホイールの`items`は1〜32件 |
| `widgets[].icon`（button） | `none`/`menu`/`back`/`close`。`none`以外ではラベルの代わりにアイコンを描きます |
| `widgets[].shape`（button） | `rectangle`（既定）、`rounded`（角の半径は短辺の1/4）、`pill`（短辺の1/2）、`ellipse`、`triangle_up`/`triangle_down`/`triangle_left`/`triangle_right`（矢印キー）、`diamond`。形の外側はタッチに反応しません |
| `widgets[].navigate`（button） | タップで表示する画面id |
| `widgets[].min`/`max`/`step`/`value`（slider/dial） | `min < max`、`1 <= step <= max - min`、`min <= value <= max` |
| `widgets[].value`（wheel） | 初期選択の添字（`0 <= value < len(items)`） |
| `widgets[].decimals`/`value`（number） | `decimals`は0〜6。`value`は`10^decimals`倍した整数の初期値（`decimals: 1, value: 1234` → `123.4`） |
| `widgets[].source`/`transparent`（image） | PNG（8ビットのグレー/RGB/RGBA/パレット）をレイアウトファイルからの相対パスで指定。矩形の大きさへ最近傍で拡縮し、RGB565で`width*height*2 <= 128 KiB`。アルファが128未満の画素は`transparent`（省略時は`#FF00FF`。アルファのないPNGで省略すると透過なし）に置き換え、同じ色の不透明画素は1段ずらします |
| `widgets[].style` | 種類ごとの既定値を省略できます（`kernel/middleware/ui/widget.hpp`の`*Style`と同じ）。imageにstyleはありません |
| `widgets[].center`（pad） | 中央ボタンを置くか（既定`true`）。パッドは32x32以上 |
| `widgets[].on_tap`、`on_press`（button/pad）、`on_change`（slider/dial/wheel/pad） | 省略可。`Dispatch()`が呼ぶメソッド名（C++識別子） |

`schema_version: 1`（トップレベルに`widgets`）のファイルはカメラ背景の`main`画面1つとして読み込み、編集コマンドで保存すると2に更新します。

## 生成されるヘッダ

`namespace`の中に`kScreenWidth`、`kScreenHeight`、`enum class ScreenId`、`enum class WidgetId`（全画面）、ホイールの選択肢`k<Id>Items[]`、画面ごとの`k<Screen>Buttons[]`/`k<Screen>Labels[]`/`k<Screen>Sliders[]`/`k<Screen>Dials[]`/`k<Screen>Wheels[]`/`k<Screen>Numbers[]`/`k<Screen>Images[]`/`k<Screen>Pads[]`（空なら省略）、`ScreenId`で引く`constexpr ui::ScreenSpec kScreens[]`と`kScreenCount`、`Dispatch()`を出します。画像があるときは同じディレクトリに`<stem>_images.hpp`（`k<Id>Pixels[]`）も書き、ヘッダから`#include`します。ビットマップは`[[gnu::section(".ui_assets")]]`を付けて出すので、`.rodata`を太らせません。ai-appのリンカスクリプトは`.ui_assets`を固定アドレスの`uai_ram_entry`より後ろに置いています（独自のリンカスクリプトでも同様に出力セクションを足してください。無ければorphanセクションとして配置されます）。実機では`ui::Screen screen(kScreens[i])`として使い、カメラ背景の画面は`LcdManagement::ComposeAndPresent()`、単色の画面は`PresentOverlay()`に渡します（[docs/middleware/ui.md](../../docs/middleware/ui.md)）。ヘッダは生成物ですがリポジトリに入れており、`generate --check`で両方が最新かどうかを確認できます。

## 今後の拡張の方針

ウィジェットの種類を増やすときは、(1) `kernel/middleware/ui`に型と`Paint`/`Update`を足し、(2) `schema.py`で検証、(3) `render.py`と`static/index.html`の描画、(4) `emit_cpp.py`の出力を揃えます。候補は次のとおりです。

| 機能 | ねらい |
| --- | --- |
| ラジオグループの宣言 | 排他選択をアプリ側の`SetChecked`呼び出しではなく定義で表現 |
| 進捗バー／ゲージ | 数値表示より直感的な状態表示 |
| 画面遷移のアニメーションや戻るスタック | 深い階層のメニュー |
| カメラ画像のPNGを背景に読み込む | 実際の映像との重なりを見ながら配置 |
| 画像の外部ストレージ配置 | RAM実行の制約（1枚128 KiB）を超える写真をフラッシュから読む |
| フォントの追加（サイズ、半角記号） | 表の単一ソース化（`canvas.cpp`の生成も`ui_designer`で行う） |

## テスト

```sh
python3 -m unittest discover -s host_app/ui_designer/tests -t host_app
```

検証（画面外、同一画面内の重なり、全画面での`id`一意性、`navigate`先、コールバック名、各種類の制約、画像パス、旧形式の読み込み）、`canvas.cpp`とのフォント表の一致、PNGの読み書きとRGB565変換（透過キー）、ボタン（各形）・アイコン・ラベル・スライダー・ダイヤル・ホイール・数値・画像・パッドの描画が実機側のテストと同じ幾何になること、生成ヘッダ（画像ヘッダを含む）の内容と`Dispatch()`の分岐（遷移を含む）、リポジトリ内のヘッダが最新であること、CLIの編集コマンド（画面の追加・改名・削除、部品の移動、各種類の追加）と`--check`、サーバのHost検査とトークン、`/api/image.png`を確認します。

`tools/make_logo.py`はai-appのロゴPNG（`userspace/ai-app/config/ui/logo.png`）をフォント表から描き直すスクリプトです。
