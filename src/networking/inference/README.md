# External decision-provider connection

Start at `inference.cppm` (`phoenix.inference`).

Owns the bounded host-local connection protocol for external Controller decision providers. Controller eligibility, fallback and final authority remain in control/execution.

Allowed dependencies: Portable data, transport and wire; no simulation or graphics.

Ownership and lifetime: Requests/results live with the connection and reset on session/provider changes. Provider replies are proposals, never already-admitted physical actions.

Relevant validation: `tests/system/inference_adapters.py`, native inference/network integration cases. These identify checks to run; this README is not execution evidence.
