# ui

RGB565のフレームに描く表示部品と、タッチ入力を受けるボタン、実行時に文字を差し替えるラベルです。`uai::ai::ui`名前空間で、OS/HALに依存せず、メモリを確保しません。フレームバッファとキャッシュ操作は呼び出し側（LCDドライバー）が持ちます。

```text
  host_app/ui_designer                       実機
  ui_layout.json ──generate──> ui_layout.hpp（ButtonSpec / LabelSpecの表、Dispatch）
                                     │
                 TouchManagement::Read() ──TouchPoint──> ButtonPanel::Update() ──Event──> Dispatch() ──> アプリのハンドラ
                                                            │
                 LcdManagement::ComposeAndPresent(capture, boxes, &group) ──> PainterGroup ──> ButtonPanel / LabelPanel
```

## Canvas

`Canvas(pixels, width, height)`はRGB565フレームへの非所有ビューです。すべての描画はフレームの範囲に切り詰めます。

| メソッド | 内容 |
| --- | --- |
| `FillRect(rect, color)` | 矩形を塗る |
| `DrawFrame(rect, thickness, color)` | 矩形の枠線を描く |
| `DrawText(x, y, text, scale, color)` | 5x7フォントで文字を描く。`scale`倍に拡大 |
| `DrawTextCentered(rect, text, scale, color)` | 矩形の中央に文字を描く |

フォントは`canvas.cpp`の`kGlyphs`にあり、英大文字、数字、空白、`- + . : / %`を持ちます。小文字は大文字として描きます。`host_app/ui_designer/font.py`は同じ表を持ち、`ui_designer validate --check-font`とユニットテストで一致を確認します。表を変えるときは両方を更新してください。

`Rgb565(r, g, b)`は8ビットRGBから16ビット色を作る`constexpr`関数です。

## ButtonSpecとButtonPanel

`ButtonSpec`はボタン1つの静的な記述です。`id`、`bounds`（`Rect`）、`label`、`ButtonStyle`（塗り、押下時の塗り、選択時の塗り`checked_fill`、枠、文字の色、文字の倍率、枠の太さ）を持ちます。これを並べた`constexpr`配列が[ui_designer](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md)の生成物で、実行時の状態は`ButtonPanel`が持ちます。

```cpp
ui::ButtonPanel buttons(app_ui::kButtons, app_ui::kButtonCount);
ui::LabelPanel labels(app_ui::kLabels, app_ui::kLabelCount);
const ui::Painter *painters[] = {&buttons, &labels};
ui::PainterGroup overlay(painters, 2U);

ui::TouchPoint sample{};
touch.Read(&sample);                      // TouchManagement
const ui::Event event = buttons.Update(sample);
app_ui::Dispatch(handlers, event);        // on_tap/on_pressに割り当てたメソッドを呼ぶ
buttons.SetChecked(id, true);             // トグルやラジオの選択状態を表示に反映
labels.SetText(id, "PERSON 7.5 FPS");
lcd.ComposeAndPresent(capture, boxes, &overlay);  // カメラ画像の上に描く
```

`Update()`はポーリングごとに1回呼びます。タッチが始まった位置がボタン内なら`kPress`を返し、そのボタンを押下状態にします。指が離れると`kTap`を返し、押下状態を解除します。塗りは押下中が`pressed_fill`、`SetChecked(id, true)`の間が`checked_fill`、それ以外が`fill`です。選択状態の意味（トグル、排他選択）はアプリが決めます。

生成ヘッダの`Dispatch(handlers, event)`は、レイアウトの`on_tap`/`on_press`に書いたメソッド名で`handlers`を呼び、呼んだら`true`を返します。メソッドがない型を渡すとコンパイルエラーになります。

## LabelSpecとLabelPanel

`LabelSpec`は文字欄の静的な記述です。`id`、`bounds`、初期文字列`text`、`LabelStyle`（文字色、背景色とその有無`has_fill`、倍率、`TextAlign`、左右の`padding`）を持ちます。`LabelPanel`はラベルごとに`kLabelTextCapacity`（64）文字のバッファを持ち、`SetText(id, text)`で差し替えます（長い文字列は切り詰め）。背景なし（`has_fill = false`）にするとカメラ画像の上に文字だけを描きます。ラベルはタッチを受けません。

## PainterとPainterGroup

`Painter`はカメラ画像をコピーした後のフレームに描く側のインターフェースで、`ButtonPanel`、`LabelPanel`、`PainterGroup`が実装します。`LcdManagement::ComposeAndPresent(capture, boxes, overlay)`の3番目の引数に渡すと、枠とマスクの後に描かれます。`PainterGroup`は複数の`Painter`を順に描き、LCDドライバーが受け取るオーバーレイを1つにまとめます。

## ai-appでの使い方

| ファイル | 内容 |
| --- | --- |
| `userspace/ai-app/config/ui_layout.json` | レイアウトの定義。`ui_designer`（ブラウザまたはCLIの`add`/`set`/`remove`）で編集します |
| `userspace/ai-app/src/ui/ui_layout.hpp` | 生成されたヘッダ。`make -C userspace/ai-app ui-layout`で再生成します |
| `userspace/ai-app/src/ui/app_ui.hpp` | `AppUi`。パネルと実行時状態を持ち、`Dispatch()`が呼ぶハンドラ（`OnPersonTap`など）を実装します。パイプラインへは`task::ModelControl`経由で触るためホストでテストできます |
| `userspace/ai-app/src/task/model_control.hpp` | `ModelBit`、`PipelineStats`、`ModelControl`。`PipelineTask`が実装し、フレームタスクはマスクにあるモデルだけに推論を投入します |
| `userspace/ai-app/src/task/camera_render_task.cpp` | 10 msごとにタッチを読んで`AppUi::HandleTouch()`へ渡し、500 msごとに`UpdateStatus()`でラベルを更新し、`VisibleBoxes()`と`Overlay()`を`ComposeAndPresent()`に渡します |

画面は次のように動きます。

| ウィジェット | 動作 |
| --- | --- |
| `PERSON` / `FACE` / `SEG`（ボタン） | タップでそのモデルの推論を有効/無効にします。有効なモデルは`checked_fill`で塗られ、無効にするとそのモデルの枠とマスクも消えます。UARTに`ui: tap id=N models=MASK` |
| `BOXES`（ボタン） | 検出枠とマスクの表示を切り替えます。UARTに`ui: tap id=N boxes=on/off` |
| `status`（ラベル、上端） | モデルごとの推論完了レートを`PERSON 7.5  FACE 7.3  SEG --  FPS`の形で表示します（`--`は無効）。パイプラインが動いていないときは`AI PIPELINE OFF` |
| `detections`（ラベル、右上、背景なし） | 最新の検出数`DET N` |

タッチコントローラの初期化に失敗した場合は`touch: controller unavailable; on-screen UI disabled`を出し、ボタンは描画されますがタッチは読みません。

## テスト

```sh
make -C kernel/middleware/ui/tests test
```

描画の範囲切り詰め、枠線、グリフ、押下から離すまでのイベント列、境界の排他、押下・選択時の塗り、ラベルの配置と差し替えと切り詰め、`PainterGroup`の描画順を確認します。`ui_layout_test`（`kernel/middleware/tests`）は生成ヘッダがコンパイルできてウィジェットが画面内にあること、およびai-appの`AppUi`を偽の`ModelControl`で動かして、モデルボタンがマスクと選択表示を切り替え、`BOXES`とマスクが表示する枠を絞り、ステータスラベルの文字列がレートから正しく作られることを確認します。
