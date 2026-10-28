# Viewscreen presentation

Start with [viewscreen.cppm](viewscreen.cppm).

GameScreen owns scene preparation, resource loading, camera and runtime overlays through private State. It consumes SessionPort projections and delegates rendering to graphics modules. Ship-anchored camera geometry comes from `phoenix.graphics.geometry`; there is no separate private camera implementation.

Dependencies: Application/session, permitted read models, graphics, Rml and file services. LoadingProgress is a shared presentation record.

Validation: Native/browser builds, existing camera cases and explicitly triggered Viewscreen/scene smoke.
