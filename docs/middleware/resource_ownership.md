# resource_ownership

一つの資源を同時に一人だけが操作できるようにする所有権の仕組みです。OSのミューテックスはバックエンドに閉じ、コアとトークンはOSの型を含みません。

| ヘッダ | 内容 |
| --- | --- |
| `resource_ownership.hpp` | `OwnershipToken`: 移動のみ可能な操作権。破棄か上書きで返却する。`ResourceOwnership<Backend>`: 一度に一つのトークンを発行する。`ResourceAccessor<Resource>`: 資源への参照とトークンをまとめたスコープ付きハンドル |
| `utkernel_mutex_backend.hpp` | `MicroTKernelMutexBackend`: `TA_INHERIT`のミューテックスを作り、`E_TMOUT`を`kTimeout`、他の失敗を`kOwnership`に変換する |

## 契約

- `Initialize()`はOS起動後に呼ぶ。二回目は`kAlreadyInitialized`。
- `Acquire(&writer, timeout)`は出力を先に空にし、未初期化なら`kNotInitialized`、nullなら`kInvalidArgument`。待ちきれなければ`kTimeout`、他の失敗は`kOwnership`。
- `Validate(writer)`は、トークンがこの所有権オブジェクトと現在のロックから発行されたものかを確認する。別の所有権オブジェクトのトークンや移動元の空トークンは`kOwnership`。
- トークンの移動元は無効になり、移動代入は代入先の旧権利を先に返す。解放はトークンごとに一回。
- 所有権オブジェクトはすべてのトークンより長生きする。タスクをまたぐ移譲は保証しない。

バックエンドは`Timeout`型、`kForever`、`Create(int *lock)`、`Lock(int, Timeout)`、`Unlock(int)`を持てばよく、ホストテストでは`tk_*_mtx`をモックした実装を使います。

## ドライバーでの使い方

```cpp
using ResourceManagement =
    resource_ownership::ResourceOwnership<resource_ownership::MicroTKernelMutexBackend>;
```

`kernel/driver/driver_ownership.hpp`がこの別名を定義し、各ドライバーは`Writer`付きのメソッドと`Accessor`を公開します。使い方は[ドライバー](../driver.md)を参照してください。

## テスト

```sh
make -C kernel/middleware/resource_ownership/tests test
```

ミューテックスの属性と呼び出し順、タイムアウトの変換、移動・自己移動・移動代入時の解放回数、別の所有権のトークン拒否を、`tk_*_mtx`のモックで確認します。優先度継承や実際の待ち時間はモックでは検証できません。
