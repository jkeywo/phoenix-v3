# Platform accessibility adapters

Start with [accessibility.cppm](accessibility.cppm). The adapters consume permitted semantic snapshots and own platform provider identities, focus/publication state and bounded action queues. They do not query an Application, session or World.

The UI compositor assembles snapshots and applies queued actions through its ordinary focus, value and authorised navigation paths. Windows owns UI Automation and IME integration; browser owns DOM/ARIA callbacks with unchanged exported names. Linux explicitly reports its unimplemented AT-SPI support rather than claiming a successful probe.

The explicit provider probe uses callbacks to the same UI paths. It remains manually triggered evidence; compilation and synthetic semantics checks do not establish actual screen-reader usability.
