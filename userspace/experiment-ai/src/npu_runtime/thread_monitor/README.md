# ThreadMonitor（experiment-ai）

`NpuRuntime`を持つタスクの状態と推論の各段階を、APP RAM上のリングバッファに記録します。

| 項目 | 値 |
| --- | --- |
| 領域 | `__sample_ai_thread_monitor_start__`から128 KiB（アドレスは`.map`で確認） |
| レコード | 64 byte。100 msごとのサンプルと、推論ごとの段階レコード |
| 保持期間 | 約102秒分。古いレコードから上書き |

各更新後にデータキャッシュをフラッシュするため、CPU停止後にデバッガーから読み出せます。電源断では消えます。

## 取得と解析

```sh
make -C userspace/experiment-ai thread-monitor
```

ELFのシンボルから領域を求め、ST-LINKのHot Plug接続でCPUを一時停止して`build-experiment-ai/thread_monitor.bin`へ読み出し、JSONとPNGを作ります。ダンプだけなら`thread-monitor-dump`を使います。出力先とCPUクロックは`THREAD_MONITOR_DUMP`、`THREAD_MONITOR_JSON`、`THREAD_MONITOR_PNG`、`THREAD_MONITOR_CPU_HZ`で変更できます。

個別に実行する場合:

```sh
python3 userspace/experiment-ai/tools/decode_thread_monitor.py trace.bin --pretty
python3 userspace/experiment-ai/tools/analyze_npu_trace.py trace.bin --top 20
uv run --project userspace/experiment-ai/tools \
  python userspace/experiment-ai/tools/visualize_thread_monitor.py \
  trace.bin --output thread_monitor.png
```

## レコードの種類

| 種類 | 内容 |
| --- | --- |
| 段階（phase） | `model_selection`、`input_preparation`、`input_preparation_wait`、`npu_execution`、`output_preparation`、`output_decoding`、`result_conversion`。終了時刻と経過時間を持ちます |
| pipeline_stage | `copy`、`resize`、`letterbox`、`input_cache`、`submit`、`irq_wait`、`epoch_continue`、`output_cache`、`decode`、`convert`、`finalize`。DWTサイクルで計測します |
| npu_epoch | 生成コードのepochごとのコールバック区間（`cpu_start`、`npu`、`cpu_end`）。DWTサイクルで計測します |
| sample | 100 msごとのタスク状態と直近のNPU時間 |

モデルIDは`0=person`、`1=segmentation`、`2=face`です。段階の開始時刻は`timestamp_ms - npu_elapsed_ms`で求めます。次フレームの入力準備は前の推論のNPU実行と重なることがあります。
