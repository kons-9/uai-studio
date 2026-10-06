# trace_format

`ai_model_monitor`と`cpu_task_monitor`がPSRAMに書く記録の形式です。ホストのデコーダ（`host_app/ai_model_monitor`、`host_app/cpu_task_monitor`）が読む契約なので、OS/HALの型を含まないヘッダに分けています。

| ヘッダ | 内容 |
| --- | --- |
| `ai_model_trace.hpp` | `ThreadMonitorTraceHeader`（64 byte）、`ThreadMonitorTraceModelName`（32 byte x 16）、`ThreadMonitorTraceRecord`（64 byte）、version=5、magic、commit marker、フラグ |
| `cpu_task_trace.hpp` | `CpuTaskMonitorTraceHeader`（64 byte）、`CpuTaskMonitorTraceTaskName`（32 byte x 33）、`CpuTaskMonitorTraceRecord`（64 byte）、version=3、magic、commit marker |

フィールド、サイズ、offsetを変えるときはversionを上げ、ホストのデコーダも同時に更新します。記録の書き込み順、commit marker、キャッシュflushは[ai_model_monitor](ai_model_monitor.md)と[cpu_task_monitor](cpu_task_monitor.md)が担当します。

## テスト

```sh
make -C kernel/middleware/trace_format/tests test
```

各構造体のサイズ、整列、デコーダの`struct`文字列に対応するoffset、既定値のmagic/version/header_sizeを確認します。
