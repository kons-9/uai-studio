# ui

RGB565のフレームに描く表示部品と、タッチ入力を受ける部品、そしてそれらをページとしてまとめる画面（`Screen`）です。`uai::ai::ui`名前空間で、OS/HALに依存せず、メモリを確保しません。フレームバッファとキャッシュ操作は呼び出し側（LCDドライバー）が持ちます。

```text
  host_app/ui_designer                       実機
  ui_layout.json ──generate──> ui_layout.hpp（ScreenSpec / ButtonSpec / LabelSpec / SliderSpecの表、Dispatch）
                                     │
                 TouchManagement::Read() ──TouchPoint──> Screen::Update() ──Event──> Dispatch() ──> アプリのハンドラ
                                                              │                                     ShowScreen()
                 カメラ画面: LcdManagement::ComposeAndPresent(capture, boxes, &screen)
                 単色画面:   LcdManagement::PresentOverlay(screen)              ──> Screen::Paint
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

## ウィジェット

静的な記述（`*Spec`）はホスト側の[ui_designer](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md)が`constexpr`配列として生成し、実行時の状態は`*Panel`が持ちます。

| 種類 | Spec | Panel | 入力 | 内容 |
| --- | --- | --- | --- | --- |
| ボタン | `ButtonSpec` | `ButtonPanel` | `kPress`、`kTap` | `label`または`Icon`（`kMenu`ハンバーガー、`kBack`矢印、`kClose`×）。`ButtonStyle`は`fill`、押下中の`pressed_fill`、`SetChecked()`中の`checked_fill`、枠、文字色、倍率、枠幅 |
| ラベル | `LabelSpec` | `LabelPanel` | なし | 実行時に`SetText(id, text)`で差し替える文字欄（`kLabelTextCapacity` = 64）。`LabelStyle`は文字色、背景色と有無、倍率、`TextAlign`、左右`padding` |
| スライダー | `SliderSpec` | `SliderPanel` | `kChange` | 横方向の値入力。`minimum`〜`maximum`を`step`刻み、見出しと現在値を上段に描画。指が触れている間はノブが追従し、値が変わるたびに`Event::value`付きの`kChange`を返す。`Value(id)`、`SetValue(id, v)` |

`ButtonPanel::Update()`と`SliderPanel::Update()`はポーリングごとに1回呼びます。タッチが始まった位置で担当する部品が決まり、ボタンは離したときに`kTap`、スライダーは動いたときに`kChange`を返します。

`Painter`はフレームに描く側のインターフェースで、各Panelと`PainterGroup`（複数のPainterを順に描く）、`Screen`が実装します。

## ScreenSpecとScreen

`ScreenSpec`は1ページ分の記述で、`Background`（`kCamera`: カメラ画像の上に描く、`kSolid`: `color`で塗りつぶしてから描く）と、そのページのボタン・ラベル・スライダーの表を持ちます。`Screen`は1つの`ScreenSpec`に対する実行時状態（3つのPanel）で、`Update()`はボタンとスライダーへ振り分け、`Paint()`は背景、ボタン、スライダー、ラベルの順に描きます。

```cpp
ui::Screen screens[] = {ui::Screen(app_ui::kScreens[0]), ui::Screen(app_ui::kScreens[1])};
ui::Screen *current = &screens[0];

const ui::Event event = current->Update(sample);
app_ui::Dispatch(handlers, event);           // on_tap/on_press/on_change と navigate を振り分け
if (current->Spec().background == ui::Background::kCamera) {
    lcd.ComposeAndPresent(capture, boxes, current);
} else {
    lcd.PresentOverlay(*current);            // カメラ画像をコピーせず、Screenが全画素を描く
}
```

生成ヘッダの`Dispatch(handlers, event)`は、レイアウトの`on_tap`/`on_press`/`on_change`に書いたメソッド名で`handlers`を呼び、`navigate`を持つボタンのタップでは`handlers.ShowScreen(ScreenId)`を呼びます。呼んだら`true`を返します。メソッドがない型を渡すとコンパイルエラーになります。

## ai-appでの使い方

| ファイル | 内容 |
| --- | --- |
| `userspace/ai-app/config/ui_layout.json` | レイアウトの定義。`ui_designer`（ブラウザまたはCLI）で編集します |
| `userspace/ai-app/src/ui/ui_layout.hpp` | 生成されたヘッダ。`make -C userspace/ai-app ui-layout`で再生成します |
| `userspace/ai-app/src/ui/app_ui.hpp` | `AppUi`。全画面の`Screen`と現在の画面を持ち、`Dispatch()`が呼ぶハンドラ（`OnPersonTap`、`OnMinConfidenceChange`、`ShowScreen`など）を実装します。パイプラインへは`task::ModelControl`経由で触るためホストでテストできます |
| `userspace/ai-app/src/task/model_control.hpp` | `ModelBit`、`PipelineStats`、`ModelControl`。`PipelineTask`が実装し、フレームタスクはマスクにあるモデルだけに推論を投入します |
| `userspace/ai-app/src/task/camera_render_task.cpp` | 10 msごとにタッチを`AppUi::HandleTouch()`へ渡し、`UpdateStatus()`でラベルを更新し、現在の画面がカメラなら`ComposeAndPresent()`、そうでなければ`PresentOverlay()`で表示します。カメラ画面でないときもキャプチャは取り出してバッファを返します |

画面は2つあります。

| 画面 | ウィジェット | 動作 |
| --- | --- | --- |
| `main`（カメラ） | `PERSON` / `FACE` / `SEG` | タップでそのモデルの推論を有効/無効。有効なモデルは`checked_fill`で塗られ、無効にすると枠とマスクも消える。UARTに`ui: tap id=N models=MASK` |
| | `BOXES` | 検出枠とマスクの表示切り替え。UARTに`ui: tap id=N boxes=on/off` |
| | ハンバーガー（右上） | `navigate: menu`で設定画面へ。UARTに`ui: screen=1` |
| | `status`（上端） | モデルごとの推論完了レート`PERSON 7.5  FACE 7.3  SEG --  FPS`（`--`は無効）。パイプライン停止時は`AI PIPELINE OFF` |
| | `detections`（右上、背景なし） | 最新の検出数`DET N` |
| `menu`（単色） | 戻る矢印（左上） | `navigate: main`でカメラ画面へ |
| | `MIN CONFIDENCE %`（スライダー） | この値未満の信頼度の枠を表示から除外。UARTに`ui: min confidence=N%` |
| | `STATUS UPDATE MS`（スライダー） | `status`ラベルの更新周期。UARTに`ui: status period=N ms` |

タッチコントローラの初期化に失敗した場合は`touch: controller unavailable; on-screen UI disabled`を出し、画面は描画されますがタッチは読みません。

## テスト

```sh
make -C kernel/middleware/ui/tests test
```

描画の範囲切り詰め、枠線、グリフ、アイコン、押下から離すまでのイベント列、境界の排他、押下・選択時の塗り、ラベルの配置と差し替えと切り詰め、スライダーの値の追従とステップ丸めと描画、`Screen`の背景塗りと振り分けを確認します。`ui_layout_test`（`kernel/middleware/tests`）は生成ヘッダがコンパイルできて全画面のウィジェットが画面内にあること、およびai-appの`AppUi`を偽の`ModelControl`で動かして、モデルボタン、`BOXES`と信頼度しきい値による枠の絞り込み、ハンバーガーと戻るによる画面遷移、スライダーのドラッグ、ステータス文字列の生成を確認します。
