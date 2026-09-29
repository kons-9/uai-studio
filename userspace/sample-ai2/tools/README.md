# sample-ai2 memory layout generator

`generate_memory_layout.py` generates the C++ static address table and the
fixed-reservation part of the GNU ld script from one layout document.  The
checked-in source is [`../config/memory_layout.yml`](../config/memory_layout.yml).

The input format is selected by the file extension and can be YAML, JSON, or
TOML.  YAML uses PyYAML when available and has a dependency-free fallback for
the schema used by this sample.  TOML uses Python 3.11+'s `tomllib`; Python
3.10 requires `tomli`.

```sh
mkdir -p /tmp/sample-ai2-memory-layout/static_memory_layout
python3 userspace/sample-ai2/tools/generate_memory_layout.py \
  --input userspace/sample-ai2/config/memory_layout.yml \
  --template userspace/sample-ai2/stm32n6570-dk-npu-ram.ld.in \
  --header /tmp/sample-ai2-memory-layout/static_memory_layout/static_memory_layout.hpp \
  --linker /tmp/sample-ai2-memory-layout/stm32n6570-dk-npu-ram.ld
```

This is intentionally a standalone tool: existing source/header/linker files
are not modified unless they are explicitly passed as output paths. To use
another format, provide the same schema in `memory_layout.json` or
`memory_layout.toml`; `--format` can override the extension when needed.

The linker template owns the application code/data sections.  It must contain
the `@MEMORY_REGIONS@` and `@STATIC_MEMORY_SECTIONS@` markers; the generator
replaces those markers and validates duplicate names, alignments, unknown
memory regions, overlaps, and fixed allocations that exceed their region.
