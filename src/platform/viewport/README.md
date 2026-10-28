# Window and drawing surface adapter

Start with [viewport.cppm](viewport.cppm).

Owns native window/browser page and canvas focus queries, plus synchronization of browser CSS dimensions with the drawing surface. The interface contains no renderer, UI or Application types. Browser JavaScript and raylib calls stay in viewport.cpp.

Dependencies: Foundation build flags and private raylib implementation.

Validation: Native/browser role builds; actual browser focus and resize journeys remain manual smoke.
