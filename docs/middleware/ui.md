# ui

RGB565のフレームに描く表示部品と、タッチ入力を受ける部品、そしてそれらをページとしてまとめる画面（`Screen`）です。`uai::ai::ui`名前空間で、OS/HALに依存せず、メモリを確保しません。フレームバッファとキャッシュ操作は呼び出し側（LCDドライバー）が持ちます。

```text
  host_app/ui_designer                       実機
  ui_layout.json ──generate──> ui_layout.hpp（ScreenSpec / ButtonSpec / LabelSpec / SliderSpec / DialSpec / WheelSpec / NumberSpec / ImageSpec / PadSpecの表、Dispatch）
  *.png          ──generate──> ui_layout_images.hpp（RGB565のビットマップ配列）
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
| `FillEllipse(rect, color)` | 矩形に内接する楕円を塗る |
| `DrawEllipseFrame(rect, thickness, color)` | 楕円の枠線を描く |
| `FillShape(shape, rect, color)` | `Shape`（下記）を矩形に内接させて塗る |
| `DrawShapeFrame(shape, rect, thickness, color)` | 形の枠線（外形と、各辺を`thickness`縮めた形との差）を描く |
| `Blit(x, y, pixels, width, height, has_transparent, transparent)` | RGB565のビットマップを貼る。`has_transparent`なら`transparent`色の画素を飛ばす |
| `DrawText(x, y, text, scale, color)` | 5x7フォントで文字を描く。`scale`倍に拡大 |
| `DrawTextCentered(rect, text, scale, color)` | 矩形の中央に文字を描く |

`Shape`はボタンの外形で、`kRectangle`、`kRounded`（角の半径は短辺の1/4）、`kPill`（短辺の1/2。横長ならカプセル、正方形なら円）、`kEllipse`、`kTriangleUp/Down/Left/Right`（矢印キー）、`kDiamond`があります。`InsideShape(shape, rect, x, y)`はその当たり判定（`constexpr`、整数演算）で、タッチ判定と描画が同じ式を使うので、見えているところだけが反応します。`InsideEllipse`はその楕円版です。

フォントは`canvas.cpp`の`kGlyphs`にあり、英大文字、数字、空白、`- + . : / %`を持ちます。小文字は大文字として描きます。`host_app/ui_designer/font.py`は同じ表を持ち、`ui_designer validate --check-font`とユニットテストで一致を確認します。表を変えるときは両方を更新してください。

`Rgb565(r, g, b)`は8ビットRGBから16ビット色を作る`constexpr`関数です。

## ウィジェット

静的な記述（`*Spec`）はホスト側の[ui_designer](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md)が`constexpr`配列として生成し、実行時の状態は`*Panel`が持ちます。

| 種類 | Spec | Panel | 入力 | 内容 |
| --- | --- | --- | --- | --- |
| ボタン | `ButtonSpec` | `ButtonPanel` | `kPress`、`kTap` | `label`または`Icon`（`kMenu`ハンバーガー、`kBack`矢印、`kClose`×）。`Shape`で外形を選ぶ（角丸、カプセル、円、三角の矢印キー、ひし形。形の外はタッチに反応しない）。`ButtonStyle`は`fill`、押下中の`pressed_fill`、`SetChecked()`中の`checked_fill`、枠、文字色、倍率、枠幅 |
| ラベル | `LabelSpec` | `LabelPanel` | なし | 実行時に`SetText(id, text)`で差し替える文字欄（`kLabelTextCapacity` = 64）。`LabelStyle`は文字色、背景色と有無、倍率、`TextAlign`、左右`padding` |
| スライダー | `SliderSpec` | `SliderPanel` | `kChange` | 横方向の値入力。`minimum`〜`maximum`を`step`刻み、見出しと現在値を上段に描画。指が触れている間はノブが追従し、値が変わるたびに`Event::value`付きの`kChange`を返す。`Value(id)`、`SetValue(id, v)` |
| ダイヤル | `DialSpec` | `DialPanel` | `kChange` | 回転式の値入力。見出しの下に円盤を置き、左下から時計回りに270°の弧で`minimum`〜`maximum`を表す（下の90°は隙間）。円盤の中でタッチした角度から値を決め、スライダーと同じ`kChange`を返す。`DialStyle`は`face`、`track`、`fill`、`pointer`、文字色、倍率、`show_value` |
| ホイール | `WheelSpec` | `WheelPanel` | `kChange` | 文字列の選択肢（最大32件）を縦に並べ、中央の帯が現在の選択。上にドラッグすると次の項目へ進み、`Event::value`は選択した添字。`Value(id)`、`SetValue(id, index)`、`ItemText(id)` |
| 数値 | `NumberSpec` | `NumberPanel` | なし | プログラムから`SetValue(id, v)`で更新する数値表示。`decimals`桁の小数（値は`10^decimals`倍の整数で持つ）と`unit`を付けて`123.4%`のように描き、`label`は小さな見出しとして左上に出す。`NumberStyle`は文字色、背景色と有無、倍率、`TextAlign` |
| 画像 | `ImageSpec` | `ImagePanel` | なし | 生成済みのRGB565ビットマップ（`pixels`、矩形と同じ大きさ）を貼る。`has_transparent`なら`transparent`色を透過。元のPNGは`ui_designer`が`ui_layout_images.hpp`に変換する |
| パッド | `PadSpec` | `PadPanel` | `kPress`、`kTap`、`kChange` | カメラ背面のコントロールホイールやゲームパッドのような丸い十字キー。上下左右の4象限と任意の中央ボタン（`has_center`）を持ち、押した象限を`PadSegment`（`kUp`、`kRight`、`kDown`、`kLeft`、`kCenter`）として`Event::value`に入れて`kPress`、同じ象限で離すと`kTap`を返す。リング上の回転は45°を1段とし、`kChange`の`value`に方向付き段数（時計回りが正）を返す。回転後の離しはタップにならず、中心や外側へ移動した間は回転を計算しない。`PadStyle`は`fill`、`pressed_fill`、`center_fill`、枠、矢印色`arrow`、枠幅 |

`ButtonPanel`、`SliderPanel`、`DialPanel`、`WheelPanel`、`PadPanel`の`Update()`はポーリングごとに1回呼びます。タッチが始まった位置で担当する部品が決まり、ボタンとパッドは離したときに`kTap`、スライダー・ダイヤル・ホイール・パッドの回転は値が変わったときに`kChange`を返します。

`Painter`はフレームに描く側のインターフェースで、各Panelと`PainterGroup`（複数のPainterを順に描く）、`Screen`が実装します。

## ScreenSpecとScreen

`ScreenSpec`は1ページ分の記述で、`Background`（`kCamera`: カメラ画像の上に描く、`kSolid`: `color`で塗りつぶしてから描く）と、そのページの各ウィジェットの表を持ちます。`Screen`は1つの`ScreenSpec`に対する実行時状態（種類ごとのPanel）で、`Update()`はボタン、スライダー、ダイヤル、ホイール、パッドへ振り分け、`Paint()`は背景、画像、ボタン、スライダー、ダイヤル、ホイール、パッド、数値、ラベルの順に描きます。

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
