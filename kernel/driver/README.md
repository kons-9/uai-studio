ドライバは
{hoge_driver}/registers/registers.hppに考えられる生レジスタの操作をできるだけ薄くラップし、
{hoge_driver}/{hoge_driver}.hppに実際の外部からの操作を提供する。

HWレジスタのキャッシング、リソース管理以外の用途で、ステートを保持しないようにすること。
基本ステート管理は上位のレイヤで行うこと。
シングルトンの場合は、resource_managerを通じて管理すること。

## 共通API

- [camera_driver](camera_driver/camera_driver.hpp): 既存AI用のメモリ管理付き撮影と、[CaptureConfiguration](camera_driver/capture_configuration.hpp)で指定する借用バッファへの2出力撮影を提供する。借用バッファは32バイト境界と十分な容量を持ち、停止成功まで存続させる。`Configure`はPipe2のcropとセンサ共通のfps/flipを変更する。露出・WB・統計領域・設定復元・復旧は同じ撮影所有権で保護する。
- [display_driver](lcd_driver/display_driver.hpp): 800x480 RGB565面の初期化・キャッシュ整合・VBlank提示・完了確認を提供する。提示した面は`Synchronize`成功まで変更・再利用しない。[LcdManagement](lcd_driver/lcd_driver.hpp)のAI合成APIも同じ所有権と表示経路を使う。
- [touch_driver](touch_driver/touch_driver.hpp): `Read`は表示範囲に補正した座標、`ReadRaw`は検査用の補正前座標を返す。LCDと共有するreset線を使うため、LCD初期化後に初期化する。
- [console_driver](console_driver/console_driver.hpp): USART1の設定・IRQ・受信キュー・排他付き送受信を提供する。受信エラーやキューあふれは、次に受け入れた入力の`error`で通知する。コマンド解析はアプリ側が行う。
- [board/time](board/time.hpp): HAL時刻取得とCPUサイクルカウンタの初期化を公開型にHAL依存を持ち込まず提供する。

カメラ・LCD・I2C・GT911・外部メモリ・ISPの共通BSP/コンポーネントはkernelターゲットで一度だけ登録する。`ai-app`・`mini-ai-app`・`hw-test`は同じ実装をリンクする。experimentのローカル実装は共有ターゲットへ統合しない。

ドライバが保持する撮影設定やISP設定は、HW設定のキャッシュと復旧に必要な情報である。試験手順・期待値・PASS/FAIL・画面構成はuserspaceに置く。

## 単体検証

```sh
cmake -S kernel/middleware/tests -B build/middleware-tests
cmake --build build/middleware-tests -j4
ctest --test-dir build/middleware-tests --output-on-failure
```

カメラ設定/ISP、表示所有権/VBlank、タッチ生座標/補正、UART受信/排他の検証はHAL-free APIまたはregister backendのstubを使う。これらは実機のIRQ・画像・クロック・起動確認を代替しない。実装後は最終ELFのシンボルと配置元を確認し、UARTを開いて書き込み、Pipe1/2開始と実機試験を確認する。
