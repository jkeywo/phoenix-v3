# Operations panel state

Start with [operations.cppm](operations.cppm).

OperationsWidgets owns officer selection, notices, supply quantity and panel readouts. Explicit context collaborators provide the permitted ConsoleModel, asset lookup, button registration and page selection; no Application is retained.

Dependencies: ConsoleModel, Rml markup/image registration and localisation. Geometry/capture stay with ConsoleSurface.

Validation: Existing personnel, away, craft, docking and logistics system cases; actual Console presentation is manually checked.
