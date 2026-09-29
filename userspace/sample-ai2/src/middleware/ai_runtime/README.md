# AI runtime (independent pipeline core)

This directory contains the host-testable core of the person pipeline.
The person-only task adapter uses this core for camera Pipe2 inference.

- `Scheduler::Submit()` registers a model-owned `AiFuture` in the pre-NPU CPU queue.
- Three `Dispatcher` instances, each with its own `ExecutionContext`, drain the
  pre-NPU CPU, NPU and post-NPU CPU queues. `RunOnce()` does not block; a task
  adapter can sleep on its queue event and invoke it upon wakeup.
- `AiFuture::Evaluate()` performs one step and returns either an error, completion,
  or the next lane and optional wait mask. Each inference gets a stable ID; trace
  callbacks receive begin/end timestamps, model ID, step ID and lane.
- An asynchronous completion calls `Signal(future, flags)` for **that future**;
  waiting entries do not occupy a dispatcher queue. Multi-bit waits support
  `kAll` and `kAny`. Completion callback fires only at pipeline termination or
  error; the model owner keeps the future and its input/output buffers alive
  through that callback.
- Capacity is eight in-flight futures total (including waiting/running). For
  concurrent RTOS dispatchers and IRQ notifications, configure a critical section
  before starting any tasks. Critical sections protect queue state; model steps,
  trace callbacks and completion callbacks run outside them. With no critical
  section installed, the core is suitable only for single-threaded tests.

Run host tests from the repository root with
`sh userspace/sample-ai2/src/middleware/ai_runtime/tests/run.sh`.

Next integration step: attach RTOS queue wakeups/critical section adapters, an NPU
IRQ-to-future notification mapping, and nonblocking NPU step transitions. The
person adapter currently installs an IRQ-safe critical section, polls empty
queues every kernel tick and waits for NPU completion inside its NPU task;
the pre/post CPU tasks can continue while it waits. On the board, inference
and camera behavior still need validation.
