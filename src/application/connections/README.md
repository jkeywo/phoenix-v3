# Connection settings

`phoenix.application.connections` presents inference bridge availability, provider selection controls and readouts through the public session interface. The session remains responsible for bridge pairing, permissions and requests.

This module receives application state explicitly and does not own a bridge, socket, worker or authoritative controller.
