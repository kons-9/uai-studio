# カーネル

`kernel/`は、μT-Kernel 3.0本体、ボード依存の起動コード、ドライバーとミドルウェアが共有する基盤をまとめた層です。アプリ（`userspace/<app>`）はこの層の上に書きます。

```text
userspace/<app>        アプリ（usermain、タスク、モデル）
kernel/middleware      ai_runtime、memory_manager、モニター、image_resizer、foundation（エラー型、ログ、タスクとメッセージの共通部品）
kernel/driver          カメラ、LCD、NPU、PSRAM、NOR、RIF、キャッシュ
kernel/utkernel        μT-Kernel 3.0 BSP2（サブモジュール）
kernel/pre_kernel      CubeMX生成コードとRAM起動
```

ドライバーもミドルウェアの`foundation`を利用します。`kernel/driver`と`kernel/middleware`は現在ai-appのビルドでだけ有効です。

## このセクションの内容

| ページ | 内容 |
| --- | --- |
| [ビルド構成](build.md) | CMakeターゲット、CMakeオプション、Makeターゲット、`local.mk` |
| [起動の流れ](boot.md) | `ram-run`からμT-Kernel起動、`usermain()`までの手順 |
| [μT-Kernel](utkernel.md) | BSP2の設定、割り込み登録、T-Monitor、フックAPI |
| [共通基盤（foundation）](common.md) | `common::Error`、`UAI_LOG_*` |
| [アプリの追加](new-app.md) | 新しい`userspace/<app>`を作る手順 |

## 設計の方針

- μT-Kernel本体とBSP2はサブモジュールとして取り込み、変更はフックAPIの実装とミューテックス数の変更など最小限にとどめています。変更点は[THIRD_PARTY_NOTICES.md](https://github.com/kons-9/uai-studio/blob/main/THIRD_PARTY_NOTICES.md)にまとめています。
- HALの初期化コードはCubeMXのIOCから生成し、ソースツリーには置きません。`kernel/pre_kernel`は生成コードとμT-Kernelの間をつなぐ最小限のコードだけを持ちます。
- アプリの追加でカーネル側の`CMakeLists.txt`を変更しなくて済むように、ルートの`CMakeLists.txt`は`APP_TARGET`で選んだアプリのディレクトリを読み込むだけにしています。
