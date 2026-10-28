# Browser main-thread services

Start at `lifecycle.cppm` (`phoenix.platform.lifecycle`).

Owns the shared browser service queue and synchronous on_main dispatch used by worker session, storage and transport adapters. The queue is declared once by the named module rather than copied through cross-module headers.

Allowed dependencies: Standard library and Emscripten proxy/thread APIs only; native and ordinary browser builds execute calls directly.

Ownership and lifetime: Worker calls retain their captured values until synchronous completion. A frontend waiting for session work pumps the same queue to avoid deadlock. This module creates no gameplay thread or scheduler.

Relevant validation: Ordinary/threaded browser integration gates; worker/storage/transport journeys exercise service dispatch. These identify checks to run; this README is not execution evidence.
