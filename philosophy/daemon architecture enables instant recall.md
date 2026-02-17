# Daemon Architecture Enables Instant Recall

A persistent daemon (`chittad`) keeps the database open and indices warm. Without this, every tool call would pay cold-start costs: loading DuckDB, ONNX model, extensions.

## Mechanism

The daemon architecture:

1. **Long-lived process** - Started once, runs continuously
2. **Pre-loaded resources** - DuckDB connections, HNSW index, VakYantra model all ready
3. **Thread pool** - Concurrent request handling (2-16 workers)
4. **Unix socket IPC** - Low-latency communication with CLI/MCP

Cold start costs avoided:
- DuckDB open: ~200ms
- VSS extension load: ~50ms
- ONNX model load: ~500ms
- Total saved per request: ~750ms

## Evidence

- **Database connection pooling** is standard practice for latency-sensitive apps
- **Model serving** systems (TensorFlow Serving, Triton) keep models loaded
- **Unix domain sockets** have ~10x lower latency than TCP loopback

## Implications

- First query as fast as hundredth query
- Memory footprint persists (~1GB with model)
- Daemon health affects all tool calls

## Trade-offs

- Memory usage even when idle
- Daemon crashes require restart
- Version updates require daemon restart

## Related Claims

- [[embeddings live separately from content for query isolation]]
- [[MCP protocol provides tool discovery]]
- [[subconscious processes what foreground attention cannot]]
