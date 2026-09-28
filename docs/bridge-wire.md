# Bridge framing and queue contract

This is the implemented C++ foundation used by the [transport worker](bridge-transport.md).
It is built and tested as `nexus_wire`. It does not authenticate peers or establish
real nanos world transport support without the runtime checkpoint.

## Frame format

The provisional NXP wire envelope uses this 12-byte header, with unsigned integers
encoded in network byte order (most significant byte first):

| Offset | Bytes | Field | Accepted value |
| --- | --- | --- | --- |
| 0 | 4 | Magic | `4e 58 50 00` (`NXP` followed by zero) |
| 4 | 2 | Wire version | 1 |
| 6 | 2 | Message type | 1: transport diagnostic |
| 8 | 4 | Payload length | 0 through 65536 inclusive, excluding the header |
| 12 | length | Payload | Opaque bytes; embedded zeros preserved |

Type 1 is reserved for the transport diagnostic. Its bytes must never be executed
as Lua or interpreted as an authenticated business request. All other types,
including zero, are rejected. Agent registration, authentication, request IDs,
transfer IDs and business schemas are not implemented or assigned type numbers.
They will have explicit contracts in their implementation milestones. Wire,
component and Lua ABI versions are independent. Changing the meaning of an
existing field or type requires a new wire version; unsupported versions/types
are errors, with no silent downgrade or negotiation in this slice.

`encode` rejects unsupported types and oversize payloads before copying them.
Its local validation errors are `std::invalid_argument`. Allocation failure is
reported by the C++ allocator; the future worker/Lua boundary must catch it after
unwinding C++ objects, never allow an exception to cross the Lua C ABI.

## Incremental decoder

One decoder belongs to one connection's I/O owner. `consume` accepts any fragment
size and returns the number of bytes used, a status and a structured error.
It stops at the end of the first complete frame even when input includes several
frames. The caller must preserve unconsumed bytes.

- `need_more`: retain the partial header/body and await further bytes.
- `ready`: call `take` once to move out the frame. Further input consumes zero
  bytes until the frame is taken; it is never overwritten or delivered twice.
- `invalid`: the stream is permanently rejected. No scanning for another magic
  sequence or automatic recovery is allowed. The transport owner must disconnect.

All header fields are checked before reserving a body buffer. The decoder retains
at most one 12-byte header plus a 64 KiB body, with ordinary container overhead.
It does not retain arbitrary coalesced input. `finish` checks EOF: a partial header
or body becomes a terminal `truncated` error; a ready frame remains available, and
an empty decoder has no error. This method reports stream completeness; the
connection owner still owns socket closure and must not feed bytes after EOF.

This bounds per-connection retained data, not the number of connections or total
process memory. The socket milestone must bound active/pending connections,
receive buffers, in-flight sends, aggregate queues and incomplete-frame duration.

## Queue behavior

`FrameQueue` is a mutex-protected FIFO ring with constructor-specified count and
wire-byte budgets. Valid count limits are 1 through 4096; byte budgets are 12 bytes
through 64 MiB. Both limits apply at once. Each queued frame costs its payload
length plus 12, so empty messages cannot evade the byte budget. Slot metadata,
allocator overhead and temporary caller buffers are additional bounded costs.
There is no unbounded backing deque or hidden retry queue.

`try_push` checks the type and payload limit, then attempts the mutex once. It
copies an admitted payload into owned storage; caller mutation cannot affect it.
`full`, `busy`, `closed` and `invalid` never enqueue any part of the message.
`try_pop` moves out one complete frame and returns its capacity to the queue.
Empty live queues return `empty`; contention returns `busy`. No operation spins
or waits for a lock on the live path. Allocation and OS scheduling still have
costs, so these operations are not a hard real-time latency guarantee.

`close` is an idempotent lifecycle operation which may wait for the short critical
section. After it returns, pushes are rejected. Already queued frames remain
drainable; subsequent pops return `closed` when empty. All users must be stopped
and joined before queue destruction. Queue closure alone does not stop workers.

The future I/O owner must retain at most one decoded frame when its destination
queue is full/busy and suspend reads instead of dropping bytes or spinning. The
Lua send boundary must return explicit backpressure; a successful enqueue will
mean accepted locally, not delivered or acknowledged by the remote application.
Retries, fair scheduling, control-event capacity and disconnect behavior belong
to the worker milestone and are not claimed by this primitive.

## Verification and remaining gate

The native tests run in Release builds without relying on disabled C assertions.
They check an independent wire vector, embedded zeros, every split of a frame,
bytewise maximum payloads, coalescing, malformed headers, unsupported versions and
types, maximum-plus-one rejection, all partial EOF positions, terminal errors,
count/byte limits, ownership, ring reuse, close/drain, and concurrent producers.
Linux CI also runs AddressSanitizer and UndefinedBehaviorSanitizer checks.

Passing these checks does not establish socket behavior, thread-safe Lua access,
host unload safety or tick impact. The socket worker uses these bounds and exposes
explicit cleanup for the separate two-server runtime checkpoint.
Only the nanos world execution thread may call Lua; workers must own native data
only. No inter-server product feature is enabled before that runtime proof passes.
