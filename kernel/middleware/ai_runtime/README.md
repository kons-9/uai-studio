# ai_runtime

推論を前処理CPU、NPU、後処理CPUの3レーンで実行する固定容量のパイプラインです。ホストPCでもビルドしてテストできます。使い方は[docs/middleware/ai_runtime.md](../../../docs/middleware/ai_runtime.md)を参照してください。

ホストテスト:

```sh
sh kernel/middleware/ai_runtime/tests/run.sh
```
