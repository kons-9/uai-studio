# 汎用周辺機能（RNG / HASH / CRC / DMA / TIM2 / RTC）

## HWの役割

`PeripheralManagement`は画像以外の周辺機能を、1つの所有権でまとめて扱うAPIです。GPIO、汎用I2C/SPI、ADCを操作する公開APIではありません。

公開ヘッダー: [peripheral_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/peripheral_driver/peripheral_driver.hpp)。名前空間は`uai::ai::peripheral`です。

| HW | 役割 | ユースケース |
| --- | --- | --- |
| RNG | HW乱数生成 | 試験データやランダム値の取得 |
| HASH | SHA-256計算 | データのハッシュ値検証 |
| CRC | CRC-32/MPEG-2計算 | 転送データのチェック値 |
| GPDMA1 / HPDMA1 Channel0 | バイト単位のメモリ間転送 | 内部RAM・外部RAM間のコピー検証 |
| TIM2 / DWT CYCCNT | 32bitカウンターとCPUサイクル観測 | 処理時間の計測 |
| RTC / LSI | 日付・時刻の設定と取得 | カレンダー機能の動作確認 |

## 初期化とAPI

共有ビルドでは`UAI_DRIVER_PERIPHERAL_TESTS`を有効にした構成でこの実装と必要なHALが追加されます。同じ構成では`npu_driver/`のソースが除外されるため、通常のai-appにNPUと一緒に自動リンクされる機能ではありません。[ビルド定義](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/CMakeLists.txt)とアプリの構成を確認してください。

専用の`Initialize()`はありません。カーネルとHALのtickが稼働し、アクセス先メモリのRIF・キャッシュが整った状態で`Acquire()`します。各操作が必要なHWの属性・クロックを用意します。

| API | 戻り値・動作 |
| --- | --- |
| `PeripheralManagement::Instance()` | 唯一の管理インスタンス |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 所有権管理を初期化して取得。`common::Error` |
| `accessor.Valid()` / `accessor.Ownership()` / `accessor->` | 有効性、Writer、Driverへのアクセス |
| `accessor.Close()` | TIM2/RTCを解放し、所有権を返す。終了エラーを明示的に確認できる |
| `CycleCount()` | DWTの現在値を`uint32_t`で読む。カウンターの有効化は行わない |

Accessorはムーブのみ可能で、破棄時にも`Close()`します。`KeepClocksOnSleep()`はありません。以下のDriver操作はWriterを最後の引数に取り、`common::Error`を返します。

| Driver API（Writerは省略表記） | 引数・動作 |
| --- | --- |
| `RandomWords(output, count, timeout_ms)` | `uint32_t`配列へcount個の乱数を取得 |
| `Sha256(input, bytes, digest, timeout_ms)` | 指定byte数のSHA-256を32byteの配列へ格納 |
| `Crc32Mpeg2(input, bytes, &value)` | CRC-32/MPEG-2の32bit値を取得 |
| `Copy(controller, source, destination, offset, bytes, timeout_ms)` | sourceとdestinationの同じoffsetからbytesをコピー |
| `StartCounter(frequency)` | TIM2を要求周波数に近い分周で開始、DWTを利用可能なら有効化 |
| `ReadCounter(&sample)` | ticks、cycles、実周波数などを取得 |
| `StopCounter()` | TIM2を停止。完全な解放はAccessorの`Close()`で行う |
| `OpenCalendar()` | LSIを使う24時間形式のRTCを初期化。既存RTCをリセットする |
| `SetCalendar(calendar)` | 日付と時刻を設定 |
| `ReadCalendar(&calendar)` | 時刻・日付を取得 |

`Memory`は`data`と実容量の`size`を持ちます。DMAの種類は`DmaController::kGeneralPurpose`（GPDMA1）か`kHighPerformance`（HPDMA1）です。

## ユースケース

以下のコード片では、カーネル起動後に取得した`PeripheralManagement::Accessor accessor`と、`const auto &writer = accessor.Ownership()`を使います。各操作の戻り値を確認してから、出力を利用してください。

### RNG: 乱数を取得する

```cpp
std::uint32_t words[8]{};
const auto status = accessor->RandomWords(words, 8, 100, writer);
```

出力はcount個の`uint32_t`を格納できる配列にします。失敗した場合、途中まで生成された値を全体の正常な結果として扱わないでください。

### HASH: SHA-256を求める

```cpp
alignas(4) std::uint8_t message[4]{'a', 'b', 'c', 0};
alignas(4) std::uint8_t digest[32]{};
const auto status = accessor->Sha256({message, sizeof(message)}, 3, digest, 100, writer);
```

計算対象は3byteですが、HW用の入力容量は4byte単位へ切り上げた分を確保します。`digest`は4byte境界の32byte配列です。SHA-256一致だけでは、データの送信元や署名の真正性を保証しません。

### CRC: CRC-32/MPEG-2を求める

```cpp
alignas(4) std::uint8_t message[12]{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
std::uint32_t checksum = 0;
const auto status = accessor->Crc32Mpeg2({message, sizeof(message)}, 9, &checksum, writer);
```

多項式`0x04c11db7`、初期値`0xffffffff`、入力/出力反転なしのCRCです。一般的なZIP/EthernetのCRC-32とは設定が異なります。既知データ`123456789`のCRC-32/MPEG-2期待値は`0x0376e6e7`です。

### GPDMA/HPDMA: バッファをコピーする

```cpp
alignas(32) static std::uint8_t source[64]{};
alignas(32) static std::uint8_t destination[64]{};
const auto status = accessor->Copy(
    uai::ai::peripheral::DmaController::kGeneralPurpose,
    {source, sizeof(source)}, {destination, sizeof(destination)}, 0, 64, 100, writer);
```

転送前後のキャッシュ操作はDriverが担当します。画素形式の変換・ブレンドにはこのAPIではなく[DMA2D](dma2d.md)を使います。

### TIM2/DWT: 処理時間を計測する

`StartCounter(1000000, writer)`などで開始し、対象処理の前後に`ReadCounter()`を呼びます。`CounterSample`の`frequency`は分周後の実際のTIM2周波数です。`cycles_available`が真のときだけ`cycles`と`cycle_frequency`を使います。

差分は32bit unsignedで求め、カウンターのwrapを扱います。経過時間はticks差を`frequency`で割ります。要求周波数が実クロックを割り切れない場合、要求値ではなく取得した実周波数で換算します。

`StopCounter()`は停止だけです。同じAccessorのまま再び`StartCounter()`すると初期化済み扱いになるため、再初期化には`Close()`後の再取得を使います。

### RTC: 日付と時刻を設定する

`OpenCalendar(writer)`を成功させてから`SetCalendar()`、`ReadCalendar()`を使います。`Calendar`のフィールドは次のとおりです。

| フィールド | 範囲 |
| --- | --- |
| year | RTCの2桁年、0〜99 |
| month / day | 月は1〜12、日は月と閏年に応じた有効日 |
| weekday | 1〜7。曜日と年月日の対応は呼び出し側が用意 |
| hours / minutes / seconds | 0〜23 / 0〜59 / 0〜59 |

`OpenCalendar()`はRTCをリセットし、Accessor終了時はRTCを無効化します。既存の保持時刻を読み出すだけのAPIや、電源断をまたぐ常設時計のAPIではありません。LSIを用いるため、精度も外部基準時計と同一ではありません。

## 制約と診断

- timeout引数は1〜1000msです。HWの初期化・終了やHAL内部の待ちもあるため、関数全体の厳密なdeadlineではありません。
- HASH/CRC入力は4byte境界で、計算長を4byte単位に切り上げた容量が必要です。メモリ容量は`INT32_MAX`以下にします。
- DMAの先頭と容量は32byte境界、範囲は32bit内、転送長は1〜65535byteです。source/destinationの全容量範囲の重複は拒否します。offsetは両バッファへ同じ値を適用します。
- HALのtimeoutは`kTimeout`、HW設定・検証・転送の失敗は`kHardware`、無効な引数は`kInvalidArgument`になります。失敗時の出力を正常データとして扱わないでください。
- 同じAccessorでは対象の周辺操作が直列になります。他Driverや直接HALから同じHWを並行操作することまでは防ぎません。
- TIM2/DWT/RTCの開始・終了はHW状態を変更します。NPU traceや別の計測器がDWTを使っている期間と重ならないようにします。
- RNG/HASH/CRCは処理ごとに終了します。DMA失敗時はabortし、必要ならChannel resetします。TIM2/RTCの終了結果が重要な場合は、destructor任せにせず`accessor.Close()`の戻り値を確認します。