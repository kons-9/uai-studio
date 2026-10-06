# ui

RGB565のフレームに描く表示部品と、タッチ入力を受けるボタンです。`uai::ai::ui`名前空間で、OS/HALに依存せず、メモリを確保しません。フレームバッファとキャッシュ操作は呼び出し側（LCDドライバー）が持ちます。

```text
  host_app/ui_designer                       実機
  ui_layout.json ──generate──> ui_layout.hpp（ButtonSpecの表）
                                     │
                 TouchManagement::Read() ──TouchPoint──> ButtonPanel::Update() ──Event──> アプリ
                                                            │
                 LcdManagement::ComposeAndPresent(..., &panel) ──> Painter::Paint(Canvas)
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

`ButtonSpec`はボタン1つの静的な記述です。`id`、`bounds`（`Rect`）、`label`、`ButtonStyle`（塗り、押下時の塗り、枠、文字の色、文字の倍率、枠の太さ）を持ちます。これを並べた`constexpr`配列が[ui_designer](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md)の生成物で、実行時の状態は`ButtonPanel`が持ちます。

```cpp
ui::ButtonPanel panel(app_ui::kButtons, app_ui::kButtonCount);
struct UiHandlers {
    bool &show_boxes;
    void OnToggleBoxesTap(const ui::Event &) { show_boxes = !show_boxes; }
} handlers{show_boxes};

ui::TouchPoint sample{};
touch.Read(&sample);                      // TouchManagement
const ui::Event event = panel.Update(sample);
app_ui::Dispatch(handlers, event);        // on_tap/on_pressに割り当てたメソッドを呼ぶ
lcd.ComposeAndPresent(capture, boxes, false, &panel);  // カメラ画像の上にボタンを描く
```

`Update()`はポーリングごとに1回呼びます。タッチが始まった位置がボタン内なら`kPress`を返し、そのボタンを押下状態にします。指が離れると`kTap`を返し、押下状態を解除します。押下中は`Paint()`が`pressed_fill`で塗ります。

生成ヘッダの`Dispatch(handlers, event)`は、レイアウトの`on_tap`/`on_press`に書いたメソッド名で`handlers`を呼び、呼んだら`true`を返します。メソッドがない型を渡すとコンパイルエラーになります。

`Painter`はカメラ画像をコピーした後のフレームに描く側のインターフェースで、`ButtonPanel`が実装します。`LcdManagement::ComposeAndPresent()`の4番目の引数に渡すと、枠とマスクの後に描かれます。

## ai-appでの使い方

| ファイル | 内容 |
| --- | --- |
| `userspace/ai-app/config/ui_layout.json` | レイアウトの定義。`ui_designer`（ブラウザまたはCLIの`add`/`set`/`remove`）で編集します |
| `userspace/ai-app/src/ui/ui_layout.hpp` | 生成されたヘッダ。`make -C userspace/ai-app ui-layout`で再生成します |
| `userspace/ai-app/src/task/camera_render_task.cpp` | 10 msごとにタッチを読み、`Dispatch()`経由で`OnToggleBoxesTap()`が検出枠の表示を切り替えます |

タッチコントローラの初期化に失敗した場合は`touch: controller unavailable; on-screen UI disabled`を出し、ボタンは描画されますがタッチは読みません。

## テスト

```sh
make -C kernel/middleware/ui/tests test
```

描画の範囲切り詰め、枠線、グリフ、押下から離すまでのイベント列、境界の排他、押下時の塗りを確認します。`ui_layout_test`（`kernel/middleware/tests`）は生成ヘッダが`ButtonSpec`としてコンパイルでき、ボタンが画面内にあり、タップが`Dispatch()`で割り当てたハンドラに届くことを確認します。
